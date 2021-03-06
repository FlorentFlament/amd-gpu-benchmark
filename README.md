AMD GPU OpenCL Benchmarking
===========================

The code in this repository allows benchmarking an AMD GPU's computing
power using the OpenCL framework against a CPU.

Installing prerequisites
------------------------

The followings need to be installed in order to have an OpenCL setup
working with an AMD GPU:

* The [amdgpu-pro driver from AMD][1]

* The [OpenCL-Headers from KhronosGroup][2] (The benchmark `Makefile`
  assumes that these headers are present in the
  `~/src/OpenCL-Headers/` directory. This can be customized.)

Running benchmarks
------------------

Provided that the OpenCL library has been properly installed (and the
`Makefile` possibly adapted), `make` builds the following binaries:

* `cl-info` provides information about the OpenCL hardware on the
  machine.

* `main` performs the actual GPU benchmark using the OpenCL
  framework. It generates the `bench_gpu_loops.txt` and
  `bench_gpu_workers.txt` files. They contain the time elapsed in
  nanoseconds (right column) for computing a given number of
  operations (left column), respectively for a single worker and for
  many workers.

* `main-cpu` performs the same computation on the computer's CPU than
  the GPU benchmark tool. This allows comparing the CPU's
  computational power against the GPU's. It generates both
  `bench_cpu_loops.txt` and `bench_cpu_workers.txt` files, which kind
  of translate the benchmark performed on the GPU, though on the
  CPU. There is almost no difference between these 2 files, since the
  computation is performed serialy anyway on the CPU.

* `gnuplot display.plot` displays a graph and prints some statistics
  from results generated by `main` and `main-cpu`.


[1]: https://www.amd.com/en/support
[2]: https://github.com/KhronosGroup/OpenCL-Headers
