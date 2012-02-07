/* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 1.3.40
 *
 * Do not make changes to this file unless you know what you are doing--modify
 * the SWIG interface file instead.
 * ----------------------------------------------------------------------------- */

package mesos;

public class ExecutorInfo {
  private long swigCPtr;
  protected boolean swigCMemOwn;

  protected ExecutorInfo(long cPtr, boolean cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = cPtr;
  }

  protected static long getCPtr(ExecutorInfo obj) {
    return (obj == null) ? 0 : obj.swigCPtr;
  }

  protected void finalize() {
    delete();
  }

  public synchronized void delete() {
    if (swigCPtr != 0) {
      if (swigCMemOwn) {
        swigCMemOwn = false;
        mesosJNI.delete_ExecutorInfo(swigCPtr);
      }
      swigCPtr = 0;
    }
  }

  public ExecutorInfo() {
    this(mesosJNI.new_ExecutorInfo__SWIG_0(), true);
  }

  public ExecutorInfo(String _uri, byte[] _initArg) {
    this(mesosJNI.new_ExecutorInfo__SWIG_1(_uri, _initArg), true);
  }

  public ExecutorInfo(String _uri, byte[] _initArg, java.util.Map<String, String> _params) {
    this(mesosJNI.new_ExecutorInfo__SWIG_2(_uri, _initArg, _params), true);
  }

  public void setUri(String value) {
    mesosJNI.ExecutorInfo_uri_set(swigCPtr, this, value);
  }

  public String getUri() {
    return mesosJNI.ExecutorInfo_uri_get(swigCPtr, this);
  }

  public void setInitArg(byte[] value) {
    mesosJNI.ExecutorInfo_initArg_set(swigCPtr, this, value);
  }

  public byte[] getInitArg() { 
    return mesosJNI.ExecutorInfo_initArg_get(swigCPtr, this); 
  }

  public void setParams(java.util.Map<String, String> value) {
    mesosJNI.ExecutorInfo_params_set(swigCPtr, this, value);
  }

  public java.util.Map<String, String> getParams() {
    return mesosJNI.ExecutorInfo_params_get(swigCPtr, this);
  }

}