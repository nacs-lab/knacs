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

* Read (from FPGA)

    The driver should keep a DMA buffer ready to be filled by the DMA hardware.
    The hardware will notify (with interupt) the driver if the buffer is filled
    or if the transfer is paused for other reasons and the driver should
    prepare new buffers to be filled by the hardware.

    When a userspace process try to read the buffer (likely using `ioctl`), the
    driver should map/copy the content that has been transferred to the
    userspace along with the length of the content and possibly indicating
    the package boundary (need to check hardware capability). If there isn't
    any finished transfer yet, it might be necessary to pause the current
    temporarily and return the content of the partial transfer (also need to
    check hardware capability).

## Implementation

* DMA buffer management

    The driver should allocate memory in the unit of pages. It should also keep
    a pool of free pages in order to speed up rapid free/allocation. We could
    also try to allocate some higher order pages (two continious physical
    pages) in order to minimize the use of scatter-gather list.

* Scatter-Gather (SG) list management

    Following the design of the Xilinx driver, the SG list should use DMA
    coherent memory (so that we don't need to explicitly map/unmap them).
    The SG list items all have the same size and doesn't need to be continious
    so we can use a memory pool and a free list to manage them.

    Due to the limit amount of DMA coherent memory, we should delay the
    creation of the SG list as much as possible. The allocation from the
    free list shouldn't be very expensive (and should be non-blocking) so we
    should create the SG list only for the buffer to be transferred and maybe
    the one that will be transferred (for both read and write).

* Write

    When the user space initiate a write, the driver should ummap the pages
    from the user process and start sending the list of pages to the hardware.
    The transfer should start immediately if there's no on-going transfer and
    should be pushed to a to-write queue otherwise.

    When the write is done, the driver should recieve an interupt from the
    hardware. The driver should check the finishing status and prepare the
    next transfer in the queue. Optionally (see above) the driver should also
    prepare to notify the user process that the transfer is done.

* Read

    The implementation of the read strongly depend on the behavior of the DMA
    engine when it reaches the end of a descriptor chain. Unfortunately, I
    couldn't find a very good document on this behavior either for the SG mode
    or for the cyclic mode. It is necessary to know this behavior in order to
    support receiving packages of unknown length. The length of the package
    should be determined by the application instead of the kernel driver. If
    it is impossible to relably pause a transfer on buffer overflow and restart
    it after new buffers are prepared, the backup plan is to require the user
    space process to initialize a transfer with known size limit. However, in
    this case we still need a way to generate an error gracefully if the
    package size is longer than the limit provided by the user space.

    We also need to decide how to transfer this data to the userspace. If the
    recieving is also initialized by the userspace. We might be able to simply
    fill the user provided/allocated buffers. The more userspace friendly way
    is to allocate the DMA buffer purely in the kernel. Then we can either
    copy the data to a user provided buffer or `mmap` the recieved pages into
    the user space. (We could also provide both and benchmark them/use
    different one in different conditions).

* Objects and lifetime

    * `knacs_dma_page`

        Cache a few free pages in a list for fast allocation

        The pages will be inserted in an `knacs_dma_block` that will be
        either mapped to the userspace or queued for DMA transfer.

        ```c
        typedef struct {
            struct rb_node node; // For chaining into a block
            knacs_dma_sg_desc *sg; // Pointer to the SG descriptor (may be NULL)
            u32 flags; // flags (may not be necessary)
            u32 idx; // page index in the block
            void *data; // actually page data (virtual address)
            dma_addr_t dma_addr; // dma (physical) address
        } knacs_dma_page;
        ```

    * `knacs_dma_block`

        This is the representation of a DMA buffer, which can be

        1. mapped to userspace

        2. Queue'd to be sent (mapped to hardware)

        3. Queue'd to be recieved (mapped to hardware)

        We may need reference counting in order to handle `unmap` from
        userspace. (Since this is what the kernel API looks like.)

        ```c
        typedef struct {
            struct list_head node; // For chaining into a queue
            struct rb_root *pages; // List of pages
            u32 flags; // flags (may not be necessary)
            atomic_t refcnt;
            // Might add other field to cache certain results
        } knacs_dma_page;
        ```

    * `knacs_dma_sg_desc`

        This is the SG descriptor for the AXI DMA engine. It needs to be
        allocated as coherent memory as mentioned above. We'll have a
        pool of coherent memory chained by a free list and allocate
        all the descriptors from there. A SG descriptor is only assigned
        to a page if it is about to be transferred since the coherent
        memory is very limitted.

* Relation to the generic DMA engine layer

    Since we are going to interface with the hardware directly, it is not
    necessary to use the generic DMA layer for data transfer. We might still
    want to user certain pieces from the generic DMA layer for certain things:

    * Mapping to hardware address (`dma_addr_t`).

        We might be able to use other kernel function to get the hardware
        address directly.

    * Acquisition of the DMA channels.

        The device appears as DMA channels in the device tree but it should be
        easy to change that and use the AXI DMA control register simply as any
        other memory mapped devices if we want to by-pass the generic DMA
        layer.
