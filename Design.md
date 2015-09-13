# Kernel driver for NaCs control system

The kernel driver is used to directly interface with the hardware (FPGA). This
is better done in the kernel space because we can have more direct and flexible
control of memory and other low level operations. Specifically:

* Access physical address (Also possible with `/dev/mem` in user space but
  requires root.)

* Allocation of coherent memory (for DMA)

* Cache control (for DMA)

* Interuption handling (for DMA). Impossible in userspace.

Additionally, by limitting the code that manipulate the hardware in the kernel
space, we can make the higher level code running as a normal user in userspace
and minimize the damage a bug in it can do on the whole system.

# Components

* Memory mapped AXI4-Lite

    This is used to transfer small amount of low latency control data to
    the FPGA.

* DMA stream

    This is used to transfer larger amount of data with a higher throughput
    and possibly higher latency. This is intended to be used for transfering
    experiment sequences and measurement data.

* User interface

    The interface with the userspace is done with the `/dev/knacs` char
    devices. The possibly useful functions are:

    * `mmap`: for memory management and direct hardware access.

    * `poll`/`read`: for notify the user process for certain events.

    * `ioctl`: for arbitrary functions.

# DMA driver

The DMA engine used in the hardware is the AXI-DMA IP. The Xilinx kernel fork
has a driver for it based on the generic DMA engine layer in the Linux kernel.
However, this driver is still WIP and is missing many features that might be
useful for our application (transfer bytes count, circular buffer mode).
Therefore, it is probably better to interact with the hardware directly instead
of using the Xilinx driver.

## User API

* Write (to FPGA)

    The driver manages (allocate, resize) the DMA buffer to be transfered. The
    user process uses `ioctl`/`mmap`/`mremap` to create/resize these buffers.
    After filling in the necessary content, the user process calls `ioctl`
    with the buffer pointer and length to initiate the transfer. The kernel
    driver might optionally (depending on whether we need this) return a token
    to the user space to infer the user process when the transfer is done
    (combined with `poll`). The initialization of the transfer should also
    invalidate the user space mapping of the DMA buffer so that the user won't
    fight with the DMA hardware. The user process should allocate another
    buffer if more transfers are needed.
