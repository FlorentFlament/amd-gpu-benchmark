#include "stdio.h"
#include "CL/cl.h"

#define N_STREAM_PROCESSORS 2304
#define BUFFER_SIZE 10

void check_success(cl_int rv, char* msg) {
  if (rv != CL_SUCCESS) {
    fprintf(stderr, "%s call failed with return code: %d\n", msg, rv);
    exit(EXIT_FAILURE);
  }
}

int main() {
  cl_int rv; // OpenCL calls return value
  const char message[] = "Hello World!";
  char host_buffer[BUFFER_SIZE];
  int data_len = BUFFER_SIZE-1;

  // Filling host buffer with our message
  for (int i=0; i < data_len; i++) {
    host_buffer[i] = message[i % (sizeof(message) - 1)];
  }
  host_buffer[data_len] = '\0';

  const char* opencl_source[] = {
    "__kernel void kerntest(__global char* data) {",
    "  size_t id = get_global_id(0);",
    "  int tmp = data[id] - 32;",
    "  for (int i=0; i<10000000; i++) {",
    "    tmp = (2*tmp + id) % 95;",
    "  }",
    "  data[id] = (char)(tmp + 32);",
    "}",
  };
  int opencl_lines_cnt = sizeof(opencl_source) / sizeof(char*);

  // Assuming only 1 platform and 1 device
  cl_platform_id platform_id;
  check_success(clGetPlatformIDs(1, &platform_id, NULL), "clGetPlatformIDs");
  cl_device_id device_id;
  check_success(clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_ALL, 1, &device_id, NULL), "clGetDeviceIDs");

  // Create the OpenCL context
  const cl_context_properties context_properties[] = {
    CL_CONTEXT_PLATFORM, (cl_context_properties)platform_id, 0
  };
  cl_context context = clCreateContext(context_properties, 1, &device_id, NULL, NULL, &rv);
  check_success(rv, "clCreateContext");

  // Create a command queue (default properties - i.e in-order host command queue)
  // It looks like the RX480 doesn't allow device size queues (cf cl-info)
  const cl_command_queue_properties q_props[] = {CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE, 0};
  cl_command_queue command_queue = clCreateCommandQueueWithProperties(context, device_id, q_props, &rv);
  check_success(rv, "clCreateCommandQueueWithProperties");

  // Create a on-device memory buffer with default properties (i.e read-write no host-mapped)
  cl_mem dev_buffer = clCreateBuffer(context, 0, BUFFER_SIZE, NULL, &rv);
  check_success(rv, "clCreateBuffer");

  // Create a program and kernel from the OpenCL source code
  cl_program program = clCreateProgramWithSource(context, opencl_lines_cnt, opencl_source, NULL, &rv);
  check_success(rv, "clCreateProgramWithSource");
  check_success(clBuildProgram(program, 1, &device_id, NULL, NULL, NULL), "clBuildProgram");

  cl_kernel kernel = clCreateKernel(program, "kerntest", &rv);
  check_success(rv, "clCreateKernel");
  check_success(clSetKernelArg(kernel, 0, sizeof(dev_buffer), &dev_buffer), "clSetKernelArg");

  // Enqueue write command
  cl_event write_ev;
  rv = clEnqueueWriteBuffer(command_queue, dev_buffer, CL_FALSE, 0, data_len,
			    host_buffer, 0, NULL, &write_ev);
  check_success(rv, "clEnqueueWriteBuffer");

  // Enqueue kernel
  size_t global_work_size[] = {4};
  cl_event exec_wait_list[] = {write_ev};
  cl_event exec_ev;
  rv = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, global_work_size,
			      NULL, 1, exec_wait_list, &exec_ev);
  check_success(rv, "clEnqueueNDRangeKernel");

  // Enqueue blocking read command
  cl_event read_wait_list[] = {exec_ev};
  rv = clEnqueueReadBuffer(command_queue, dev_buffer, CL_TRUE, 0, data_len,
			   host_buffer, 1, read_wait_list, NULL);
  check_success(rv, "clEnqueueReadBuffer");

  printf("Message read: %s\n", host_buffer);

  // Some profiling
  cl_ulong exec_start;
  cl_ulong exec_end;
  rv = clGetEventProfilingInfo(exec_ev, CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &exec_start, NULL);
  check_success(rv, "clGetEventProfilingInfo (COMMAND_START)");
  rv = clGetEventProfilingInfo(exec_ev, CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &exec_end, NULL);
  check_success(rv, "clGetEventProfilingInfo (COMMAND_END)");
  printf("Kernel execution time: %lu\n", exec_end - exec_start);

  // Free resources
  check_success(clReleaseMemObject(dev_buffer), "clReleaseMemObject");
  check_success(clReleaseCommandQueue(command_queue), "clReleaseCommandQueue");
  check_success(clReleaseContext(context), "clReleaseContext");
}
