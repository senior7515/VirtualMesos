#include <stdlib.h>
#include <unistd.h>

#include <algorithm>

#include "lxc_isolation_module.hpp"

#include "common/foreach.hpp"

#include "launcher/launcher.hpp"

using std::cerr;
using std::cout;
using std::endl;
using std::list;
using std::make_pair;
using std::max;
using std::ostringstream;
using std::pair;
using std::queue;
using std::string;
using std::vector;

using boost::lexical_cast;
using boost::unordered_map;
using boost::unordered_set;

using namespace mesos;
using namespace mesos::internal;
using namespace mesos::internal::launcher;
using namespace mesos::internal::slave;

namespace {

const int32_t CPU_SHARES_PER_CPU = 1024;
const int32_t MIN_CPU_SHARES = 10;
const int64_t MIN_RSS = 128 * Megabyte;

}


LxcIsolationModule::LxcIsolationModule()
  : initialized(false) {}


LxcIsolationModule::~LxcIsolationModule()
{
  // We want to wait until the reaper has completed because it
  // accesses 'this' in order to make callbacks ... deleting 'this'
  // could thus lead to a seg fault!
  if (initialized) {
    CHECK(reaper != NULL);
    process::post(reaper->self(), process::TERMINATE);
    process::wait(reaper->self());
    delete reaper;
  }
}


void LxcIsolationModule::initialize(Slave* slave)
{
  this->slave = slave;
  
  // Run a basic check to see whether Linux Container tools are available
  if (system("lxc-version > /dev/null") != 0) {
    LOG(FATAL) << "Could not run lxc-version; make sure Linux Container "
                << "tools are installed";
  }

  // Check that we are root (it might also be possible to create Linux
  // containers without being root, but we can support that later)
  if (getuid() != 0) {
    LOG(FATAL) << "LXC isolation module requires slave to run as root";
  }

  reaper = new Reaper(this);
  process::spawn(reaper);
  initialized = true;
}


void LxcIsolationModule::launchExecutor(Framework* framework, Executor* executor)
{
  if (!initialized) {
    LOG(FATAL) << "Cannot launch executors before initialization!";
  }

  infos[framework->frameworkId][executor->info.executor_id()] = new FrameworkInfo();

  LOG(INFO) << "Starting executor for framework " << framework->frameworkId << ": "
            << executor->info.uri();

  // Get location of Mesos install in order to find mesos-launcher.
  string mesosHome = slave->getConfiguration().get("home", ".");
  string mesosLauncher = mesosHome + "/mesos-launcher";

  // Create a name for the container
  ostringstream oss;
  oss << "mesos.slave-" << slave->slaveId
      << ".framework-" << framework->frameworkId;
  string containerName = oss.str();

  infos[framework->frameworkId][executor->info.executor_id()]->container = containerName;
  executor->executorStatus = "Container: " + containerName;

  // Run lxc-execute mesos-launcher using a fork-exec (since lxc-execute
  // does not return until the container is finished). Note that lxc-execute
  // automatically creates the container and will delete it when finished.
  pid_t pid;
  if ((pid = fork()) == -1)
    PLOG(FATAL) << "Failed to fork to launch lxc-execute";

  if (pid) {
    // In parent process
    infos[framework->frameworkId][executor->info.executor_id()]->lxcExecutePid = pid;
    LOG(INFO) << "Started child for lxc-execute, pid = " << pid;
    int status;
  } else {
    // Create an ExecutorLauncher to set up the environment for executing
    // an external launcher_main.cpp process (inside of lxc-execute).

    const Configuration& conf = slave->getConfiguration();

    map<string, string> params;

    for (int i = 0; i < framework->info.executor().params().param_size(); i++) {
      params[framework->info.executor().params().param(i).key()] = 
	framework->info.executor().params().param(i).value();
    }

    ExecutorLauncher* launcher;
    launcher =
      new ExecutorLauncher(framework->frameworkId,
			   executor->info.executor_id(),
			   executor->info.uri(),
			   framework->info.user(),
			   slave->getUniqueWorkDirectory(framework->frameworkId,
							 executor->info.executor_id()),
			   slave->self(),
			   conf.get("frameworks_home", ""),
			   conf.get("home", ""),
			   conf.get("hadoop_home", ""),
			   !slave->local,
			   conf.get("switch_user", true),
			   params);
    launcher->setupEnvironmentForLauncherMain();
    
    // Run lxc-execute.
    execlp("lxc-execute", "lxc-execute", "-n", containerName.c_str(),
           mesosLauncher.c_str(), (char *) NULL);
    // If we get here, the execl call failed.
    fatalerror("Could not exec lxc-execute");
    // TODO: Exit the slave if this happens
  }
}


void LxcIsolationModule::killExecutor(Framework* framework, Executor* executor)
{
  string container = infos[framework->frameworkId][executor->info.executor_id()]->container;
  if (container != "") {
    LOG(INFO) << "Stopping container " << container;
    int ret = shell("lxc-stop -n %s", container.c_str());
    if (ret != 0)
      LOG(ERROR) << "lxc-stop returned " << ret;
    infos[framework->frameworkId][executor->info.executor_id()]->container = "";
    executor->executorStatus = "No executor running";
    delete infos[framework->frameworkId][executor->info.executor_id()];
    infos[framework->frameworkId].erase(executor->info.executor_id());
  }
}


void LxcIsolationModule::resourcesChanged(Framework* framework, Executor* executor)
{
  if (infos[framework->frameworkId][executor->info.executor_id()]->container != "") {
    // For now, just try setting the CPUs and memory right away, and kill the
    // framework if this fails.
    // A smarter thing to do might be to only update them periodically in a
    // separate thread, and to give frameworks some time to scale down their
    // memory usage.

    double cpu = executor->resources.getScalar("cpu", Resource::Scalar()).value();
    int32_t cpuShares = max(CPU_SHARES_PER_CPU * (int32_t) cpu, MIN_CPU_SHARES);
    if (!setResourceLimit(framework, executor, "cpu.shares", cpuShares)) {
      // Tell slave to kill framework, which will invoke killExecutor.
      slave->killFramework(framework);
      return;
    }

    double mem = executor->resources.getScalar("mem", Resource::Scalar()).value();
    int64_t rssLimit = max((int64_t) mem, MIN_RSS) * 1024LL * 1024LL;
    if (!setResourceLimit(framework, executor, "memory.limit_in_bytes", rssLimit)) {
      // Tell slave to kill framework, which will invoke killExecutor.
      slave->killFramework(framework);
      return;
    }
  }
}


bool LxcIsolationModule::setResourceLimit(Framework* framework,
					  Executor* executor,
                                          const string& property,
                                          int64_t value)
{
  LOG(INFO) << "Setting " << property << " for framework " << framework->frameworkId
            << " to " << value;
  int ret = shell("lxc-cgroup -n %s %s %lld",
                  infos[framework->frameworkId][executor->info.executor_id()]->container.c_str(),
                  property.c_str(),
                  value);
  if (ret != 0) {
    LOG(ERROR) << "Failed to set " << property << " for framework " << framework->frameworkId
               << ": lxc-cgroup returned " << ret;
    return false;
  } else {
    return true;
  }
}


int LxcIsolationModule::shell(const char* fmt, ...)
{
  char *cmd;
  FILE *f;
  int ret;
  va_list args;
  va_start(args, fmt);
  if (vasprintf(&cmd, fmt, args) == -1)
    return -1;
  if ((f = popen(cmd, "w")) == NULL)
    return -1;
  ret = pclose(f);
  if (ret == -1)
    LOG(INFO) << "pclose error: " << strerror(errno);
  free(cmd);
  va_end(args);
  return ret;
}


LxcIsolationModule::Reaper::Reaper(LxcIsolationModule* m)
  : module(m)
{}

  
void LxcIsolationModule::Reaper::operator () ()
{
  link(module->slave->self());
  while (true) {
    receive(1);
    if (name() == process::TIMEOUT) {
      // Check whether any child process has exited
      pid_t pid;
      int status;
      if ((pid = waitpid((pid_t) -1, &status, WNOHANG)) > 0) {
        foreachpair (const FrameworkID& frameworkId, _, module->infos) {
          foreachpair (const ExecutorID& executorId, FrameworkInfo* info, module->infos[frameworkId]) {
	    if (info->lxcExecutePid == pid) {
	      info->lxcExecutePid = -1;
	      info->container = "";
	      LOG(INFO) << "Telling slave of lost framework " << frameworkId;
	      // TODO(benh): This is broken if/when libprocess is parallel!
	      module->slave->executorExited(frameworkId, executorId, status);
	      delete module->infos[frameworkId][executorId];
	      module->infos[frameworkId].erase(executorId);
	      break;
	    }
	  }
	}
      }
    } else if (name() == process::TERMINATE || name() == process::EXITED) {
      return;
    }
  }
}
