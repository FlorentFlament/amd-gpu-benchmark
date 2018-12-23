#include <stdio.h>
#include "CL/cl.h"

#define BUFFER_SIZE (50000 + 1)

#define DEFAULT_WORKERS_CNT 1024
#define DEFAULT_LOOPS_CNT 100000

#define NWORKERS_START 1000
#define NWORKERS_STEP 1000
#define NWORKERS_END 50000

#define LOOPCNT_START 10000
#define LOOPCNT_STEP 10000
#define LOOPCNT_END 500000

#define SOURCE_SIZE 1024

/*
 * be_* for bench specific structures.
 */

typedef struct _be_device {
  cl_platform_id platform_id;
  cl_device_id device_id;
  cl_context context;
  cl_command_queue command_queue;
} be_device;

typedef struct _be_kernel {
  const be_device *device;
  cl_program program;
  cl_kernel kernel;
  cl_mem buffer;
} be_kernel;

void check_success(cl_int rv, char* msg) {
  if (rv != CL_SUCCESS) {
    fprintf(stderr, "%s call failed with return code: %d\n", msg, rv);
    exit(EXIT_FAILURE);
  }
}

/*
 * Initialize an OpenCL device for our benchmarks.
 * Assuming only 1 platform and 1 device.
 */
cl_int be_device_init(be_device* dev) {
  cl_int rv;
  rv = clGetPlatformIDs(1, &(dev->platform_id), NULL);
  if (rv) return rv;
  rv = clGetDeviceIDs(dev->platform_id, CL_DEVICE_TYPE_ALL, 1, &(dev->device_id), NULL);
  if (rv) return rv;

  // Create the OpenCL context
  const cl_context_properties context_properties[] = {
    CL_CONTEXT_PLATFORM, (cl_context_properties)dev->platform_id, 0
  };
  dev->context = clCreateContext(context_properties, 1, &(dev->device_id), NULL, NULL, &rv);
  if (rv) return rv;

  // Create a command queue
  // RX480 doesn't allow device side queues (cf cl-info)
  const cl_command_queue_properties q_props[] = {CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE, 0};
  dev->command_queue = clCreateCommandQueueWithProperties(dev->context, dev->device_id, q_props, &rv);
  if (rv) return rv;

  return 0;
}

cl_int be_device_release(be_device *dev) {
  cl_int rv;
  rv = clReleaseCommandQueue(dev->command_queue);
  if (rv) return rv;
  rv = clReleaseContext(dev->context);
  if (rv) return rv;
  return 0;
}

cl_int be_kernel_init(const be_device *dev, be_kernel *be_kern) {
  cl_int rv;
  const char *source =
    "__kernel void kerntest(__global char* data, int loops_cnt) {"
    "  size_t id = get_global_id(0);"
    "  int tmp = data[id] - 32;"
    "  for (int i=0; i<loops_cnt; i++) {"
    "    tmp = (2*tmp + id) % 95;"
    "  }"
    "  data[id] = (char)(tmp + 32);"
    "}";

  be_kern->device = dev;

  // Create a program and kernel from the OpenCL source code
  be_kern->program = clCreateProgramWithSource(dev->context, 1, &source, NULL, &rv);
  if (rv) return rv;
  rv = clBuildProgram(be_kern->program, 1, &(dev->device_id), NULL, NULL, NULL);
  if (rv) return rv;
  be_kern->kernel = clCreateKernel(be_kern->program, "kerntest", &rv);
  if (rv) return rv;

  // Create a on-device memory buffer with default properties (i.e read-write no host-mapped)
  be_kern->buffer = clCreateBuffer(dev->context, 0, BUFFER_SIZE, NULL, &rv);
  if (rv) return rv;
  rv = clSetKernelArg(be_kern->kernel, 0, sizeof(be_kern->buffer), &(be_kern->buffer));
  if (rv) return rv;

  return 0;
}

/*
 * This doesn't release kern->device
 */
cl_int be_kernel_release(be_kernel *kern) {
  cl_int rv;
  rv = clReleaseMemObject(kern->buffer);
  if (rv) return rv;
  rv = clReleaseKernel(kern->kernel);
  if (rv) return rv;
  rv = clReleaseProgram(kern->program);
  if (rv) return rv;
  return 0;
}

cl_int be_kernel_set_loopcnt(const be_kernel *kern, int loop_cnt) {
  return clSetKernelArg(kern->kernel, 1, sizeof(loop_cnt), &loop_cnt);
}

cl_int be_kernel_run(const be_kernel* kern, size_t n_workers, size_t* duration) {
  cl_event exec_ev;
  cl_int rv = clEnqueueNDRangeKernel(kern->device->command_queue, kern->kernel,
				     1, NULL, &n_workers, NULL, 0, NULL, &exec_ev);
  if (rv) return rv;
  clWaitForEvents(1, &exec_ev);

  // Profiling
  cl_ulong exec_start;
  cl_ulong exec_end;
  rv = clGetEventProfilingInfo(exec_ev, CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &exec_start, NULL);
  if (rv) return rv;
  rv = clGetEventProfilingInfo(exec_ev, CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &exec_end, NULL);
  if (rv) return rv;

  *duration = exec_end - exec_start;
  return 0;
}

cl_int do_bench_loops(const be_kernel *kern, int start, int end, int step) {
  cl_int rv;
  for (int loops_cnt = start; loops_cnt <= end; loops_cnt += step) {
    rv = be_kernel_set_loopcnt(kern, loops_cnt);
    if (rv) return rv;

    size_t duration;
    rv = be_kernel_run(kern, DEFAULT_WORKERS_CNT, &duration);
    if (rv) return rv;

    printf("%d\t%lu\n", loops_cnt, duration);
    fflush(stdout);
  }
  return 0;
}

cl_int do_bench_workers(const be_kernel *kern, int start, int end, int step) {
  cl_int rv;
  rv = be_kernel_set_loopcnt(kern, DEFAULT_LOOPS_CNT);
  if (rv) return rv;

  for (size_t n_workers = start; n_workers <= end; n_workers += step) {
    size_t duration;
    rv = be_kernel_run(kern, n_workers, &duration);
    if (rv) return rv;

    printf("%zd\t%lu\n", n_workers, duration);
    fflush(stdout);
  }
  return 0;
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

  be_device bench_dev;
  rv = be_device_init(&bench_dev);
  check_success(rv, "be_device_init");

  be_kernel bench_kern;
  rv = be_kernel_init(&bench_dev, &bench_kern);
  check_success(rv, "be_kernel_init");

  // Write initial data set into device buffer
  cl_event write_ev;
  rv = clEnqueueWriteBuffer(bench_dev.command_queue, bench_kern.buffer, CL_FALSE, 0, data_len,
			    host_buffer, 0, NULL, &write_ev);
  check_success(rv, "clEnqueueWriteBuffer");
  clWaitForEvents(1, &write_ev);

  rv = do_bench_loops(&bench_kern, LOOPCNT_START, LOOPCNT_END, LOOPCNT_STEP);
  check_success(rv, "do_bench_loops");

  //rv = do_bench_workers(&bench_kern, NWORKERS_START, NWORKERS_END, NWORKERS_STEP);
  //check_success(rv, "do_bench_workers");

  // Enqueue blocking read command
  rv = clEnqueueReadBuffer(bench_dev.command_queue, bench_kern.buffer, CL_TRUE, 0, data_len,
			   host_buffer, 0, NULL, NULL);
  check_success(rv, "clEnqueueReadBuffer");
  // printf("%s\n", host_buffer);

  // Free resources
  rv = be_kernel_release(&bench_kern);
  check_success(rv, "be_kernel_release");
  rv = be_device_release(&bench_dev);
  check_success(rv, "be_device_release");
}
