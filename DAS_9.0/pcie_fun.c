/**
 * @file pcie_fun.c
 * @brief Windows XDMA 设备节点访问的 C 层封装。
 *
 * 本文件负责发现 XDMA 设备、打开 user/c2h/h2c/event 节点，并把较大的读写请求拆分为
 * MAX_BYTES_PER_TRANSFER 大小的块。上层 receive_data 只经由 pcie_fun.h 的稳定接口访问它。
 * 所有 HANDLE 和对齐缓冲为进程级状态，初始化与释放必须成对执行。
 */
#ifdef __cplusplus
extern "C" {
#endif
#include <Windows.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <strsafe.h>
#include <stdint.h>
#include <SetupAPI.h>
#include <INITGUID.H>
#include <WinIoCtl.h>
#include "pcie_fun.h"

//#include <AtlBase.h>
#include <io.h>
#include "xdma_public.h"
#pragma comment(lib, "setupapi.lib")


#define FPGA_DDR_START_ADDR 0x00000000
#define MAX_BYTES_PER_TRANSFER 0x800000

static HANDLE h_c2h0;
static HANDLE h_h2c0;
static HANDLE h_user;
static HANDLE h_event0;
static HANDLE h_event1;
static HANDLE h_event2;
static HANDLE h_event3;

static DWORD  user_start_en;
static char   base_path[MAX_PATH + 1] = "";


static unsigned int c2h_fpga_ddr_addr[8];
static unsigned int h2c_fpga_ddr_addr[8];
unsigned char *h2c_align_mem_tmp;
unsigned char *c2h_align_mem_tmp;

unsigned int image_h;
unsigned int image_v;

static unsigned char fbuf;

LARGE_INTEGER start;
LARGE_INTEGER stop;
LARGE_INTEGER freq;



/**
 * @brief 输出驱动访问诊断信息。
 * @param fmt printf 风格格式串。
 * @return vprintf 的返回值。
 * @note 当前实现始终输出；保留为集中控制底层日志的入口。
 */
static int verbose_msg(const char* const fmt, ...) {

    int ret = 0;
    va_list args;
    if (1) {
        va_start(args, fmt);
        ret = vprintf(fmt, args);
        va_end(args);
    }
    return ret;

}
/**
 * @brief 分配满足 DMA 对齐要求的主机缓冲。
 * @param size 所需字节数；传入 0 时至少分配 4 字节。
 * @param alignment 对齐边界；传入 0 时使用系统页大小。
 * @return _aligned_malloc 返回的地址，失败时为 NULL。
 */
static BYTE* allocate_buffer(size_t size, size_t alignment) {

    if (size == 0) {
        size = 4;
    }

    if (alignment == 0) {
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        alignment = sys_info.dwPageSize;
        //printf("alignment = %d\n",alignment);
    }
    verbose_msg("Allocating host-side buffer of size %d, aligned to %d bytes\n", size, alignment);
    return (BYTE*)_aligned_malloc(size, alignment);

}

/**
 * @brief 枚举指定 GUID 的已连接设备，并取得设备接口基础路径。
 * @param guid XDMA 设备接口 GUID。
 * @param devpath 输出 ANSI 路径缓冲。
 * @param len_devpath 输出缓冲长度。
 * @return 枚举到的接口数量；无法创建设备信息集合时终止进程。
 */
static int get_devices(GUID guid, char* devpath, size_t len_devpath) {

    SP_DEVICE_INTERFACE_DATA device_interface;
    PSP_DEVICE_INTERFACE_DETAIL_DATA dev_detail;
    DWORD index;
    HDEVINFO device_info;
    wchar_t tmp[256];
    device_info = SetupDiGetClassDevs((LPGUID)&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "GetDevices INVALID_HANDLE_VALUE\n");
        exit(-1);
    }

    device_interface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    // enumerate through devices

    for (index = 0; SetupDiEnumDeviceInterfaces(device_info, NULL, &guid, index, &device_interface); ++index) {

        // get required buffer size
        ULONG detailLength = 0;
        if (!SetupDiGetDeviceInterfaceDetail(device_info, &device_interface, NULL, 0, &detailLength, NULL) && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            fprintf(stderr, "SetupDiGetDeviceInterfaceDetail - get length failed\n");
            break;
        }

        // allocate space for device interface detail
        dev_detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, detailLength);
        if (!dev_detail) {
            fprintf(stderr, "HeapAlloc failed\n");
            break;
        }
        dev_detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        // get device interface detail
        if (!SetupDiGetDeviceInterfaceDetail(device_info, &device_interface, dev_detail, detailLength, NULL, NULL)) {
            fprintf(stderr, "SetupDiGetDeviceInterfaceDetail - get detail failed\n");
            HeapFree(GetProcessHeap(), 0, dev_detail);
            break;
        }

        StringCchCopy(tmp, len_devpath, dev_detail->DevicePath);
        wcstombs(devpath, tmp, 256);
        HeapFree(GetProcessHeap(), 0, dev_detail);
    }

    SetupDiDestroyDeviceInfoList(device_info);

    return index;
}


/**
 * @brief 打开 XDMA 设备目录下的一个命名节点。
 * @param device_base_path SetupAPI 返回的设备基础路径。
 * @param device_name 节点后缀，例如 "\\user"、"\\c2h_0"。
 * @param accessFlags CreateFile 所需的读写权限。
 * @return 有效 Win32 HANDLE，失败时为 INVALID_HANDLE_VALUE。
 */
HANDLE open_devices(char* device_base_path, char* device_name, DWORD accessFlags)
{
    char device_path[MAX_PATH + 1] = "";
    wchar_t device_path_w[MAX_PATH + 1];
    HANDLE h;

    // extend device path to include target device node (xdma_control, xdma_user etc)
    verbose_msg("Device base path: %s\n", device_base_path);
    strcpy_s(device_path, sizeof device_path, device_base_path);
    strcat_s(device_path, sizeof device_path, device_name);
    verbose_msg("Device node: %s\n", device_name);
    // open device file
    mbstowcs(device_path_w, device_path, sizeof(device_path));
    h = CreateFile(device_path_w, accessFlags, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Error opening device, win32 error code: %ld\n", GetLastError());
    }

    return h;

}
/**
 * @brief 打开 XDMA 的 event_0 设备节点，供事件同步调用。
 * @return 成功返回 1，打开失败返回 0。
 */
int open_event()
{
    char event0_name[] = "\\event_0";


    h_event0 = open_devices(base_path, event0_name, GENERIC_READ);
    if (h_event0 == INVALID_HANDLE_VALUE) return 0;


    return 1;

}
// static int read_device(HANDLE device, long address, DWORD size, uint32_t *value)
// {
//     DWORD rd_size = 0;
//     BYTE buffer[4] = {0}; // 假设读取4字节数据，适配uint32_t
//     unsigned int transfers;
//     unsigned int i;

//     // 检查输入参数
//     if (size > sizeof(buffer)) {
//         fprintf(stderr, "Requested size %lu exceeds maximum supported size %zu\n", size, sizeof(buffer));
//         return -4; // 新增错误码，表示请求大小超出限制
//     }

//     // 设置文件指针
//     if (INVALID_SET_FILE_POINTER == SetFilePointer(device, address, NULL, FILE_BEGIN)) {
//         fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
//         return -3;
//     }

//     // 计算分块传输次数
//     transfers = (unsigned int)(size / MAX_BYTES_PER_TRANSFER);
//     for (i = 0; i < transfers; i++) {
//         if (!ReadFile(device, (void *)(buffer + i * MAX_BYTES_PER_TRANSFER), (DWORD)MAX_BYTES_PER_TRANSFER, &rd_size, NULL)) {
//             fprintf(stderr, "ReadFile failed, win32 error code: %ld\n", GetLastError());
//             return -1;
//         }
//         if (rd_size != MAX_BYTES_PER_TRANSFER) {
//             fprintf(stderr, "Read size mismatch: expected %lu, got %lu\n", MAX_BYTES_PER_TRANSFER, rd_size);
//             return -2;
//         }
//     }

//     // 读取剩余部分
//     if (!ReadFile(device, (void *)(buffer + i * MAX_BYTES_PER_TRANSFER), (DWORD)(size - i * MAX_BYTES_PER_TRANSFER), &rd_size, NULL)) {
//         fprintf(stderr, "ReadFile failed for remaining data, win32 error code: %ld\n", GetLastError());
//         return -1;
//     }
//     if (rd_size != (size - i * MAX_BYTES_PER_TRANSFER)) {
//         fprintf(stderr, "Read size mismatch for remaining data: expected %lu, got %lu\n",
//                 size - i * MAX_BYTES_PER_TRANSFER, rd_size);
//         return -2;
//     }

//     // 将读取到的字节数据转换为uint32_t
//     *value = 0;
//     memcpy(value, buffer, size); // 根据size将buffer中的数据拷贝到value

//     return size; // 成功时返回读取的字节数
// }
/**
 * @brief 从一个 XDMA 文件节点的指定偏移连续读取字节块。
 *
 * 使用 SetFilePointer 定位后按 MAX_BYTES_PER_TRANSFER 分块读取；仅当请求字节全部读完时成功。
 * @return 0 成功；-3 定位失败；-1 ReadFile 失败；-2 设备返回零字节。
 */
static int read_device(HANDLE device, long address, DWORD size, BYTE *buffer)
{
    if (INVALID_SET_FILE_POINTER == SetFilePointer(device, address, NULL, FILE_BEGIN)) {
        fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
        return -3;
    }

    DWORD total_read = 0;
    while (total_read < size) {
        const DWORD request_size =
            ((DWORD)MAX_BYTES_PER_TRANSFER < (size - total_read))
                ? (DWORD)MAX_BYTES_PER_TRANSFER
                : (size - total_read);
        DWORD rd_size = 0;

        if (!ReadFile(device, buffer + total_read, request_size, &rd_size, NULL)) {
            return -1;
        }

        if (rd_size == 0) {
            return -2;
        }

        total_read += rd_size;
    }

    return 0;
}

/**
 * @brief 向一个 XDMA 文件节点的指定偏移连续写入字节块。
 *
 * 写入策略与 read_device 对称：定位后循环写块，直到全部数据完成。
 * @return 成功时返回 size；-3 定位失败；-1 写失败；-2 设备返回零字节。
 */
static int write_device(HANDLE device, long address, DWORD size, BYTE *buffer)
{
    if (INVALID_SET_FILE_POINTER == SetFilePointer(device, address, NULL, FILE_BEGIN)) {
        fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
        return -3;
    }

    DWORD total_written = 0;
    while (total_written < size) {
        const DWORD request_size =
            ((DWORD)MAX_BYTES_PER_TRANSFER < (size - total_written))
                ? (DWORD)MAX_BYTES_PER_TRANSFER
                : (size - total_written);
        DWORD wr_size = 0;

        if (!WriteFile(device, buffer + total_written, request_size, &wr_size, NULL)) {
            return -1;
        }

        if (wr_size == 0) {
            return -2;
        }

        total_written += wr_size;
    }
    return size;
}

/**
 * @brief 从 FPGA DDR 的 C2H 通道读取一段数据。
 * @param address 设备侧字节偏移。
 * @param size 读取字节数。
 * @param buffer 调用方提供的目标缓冲。
 * @return read_device 的状态码。
 */
int c2h_transfer(unsigned int address,unsigned int size,unsigned char *buffer)
{
    return read_device(h_c2h0, address, size, buffer);
}

/**
 * @brief 通过 H2C 通道向 FPGA DDR 写入一段数据。
 *
 * 为满足 DMA 对齐，先复制到初始化阶段分配的 h2c_align_mem_tmp，再执行实际写入。
 * @return write_device 的返回值。
 */
int h2c_transfer(unsigned int address,unsigned int size,unsigned char *buffer)
{
    memcpy(h2c_align_mem_tmp, buffer, size);
    return write_device(h_h2c0, address, size, h2c_align_mem_tmp);
}


/** @brief 向 XDMA user 节点的指定偏移写入原始字节；错误码由底层函数忽略。 */
void user_write(unsigned int address,unsigned int size,unsigned char *buffer)
{
    write_device(h_user,address,size,buffer);
}

/** @brief 从 XDMA user 节点的指定偏移读取原始字节；错误码由底层函数忽略。 */
void user_read(unsigned int address,unsigned int size,unsigned char *buffer)
{
    read_device(h_user,address,size,buffer);
}

/** @brief 读取 user 控制空间的一个 32 位寄存器。 */
void read_control(long addr,uint32_t *val )
{
    read_device(h_user,addr,4,(BYTE *)val);
}
/** @brief 写入 user 控制空间的一个 32 位寄存器。 */
void write_control(long addr,uint32_t val)
{
    write_device(h_user,addr,4,(BYTE *)&val);
}
/**
 * @brief 关闭 PCIe 访问通道并清除启动/中断使能寄存器。
 *
 * 调用方必须确保不再有采集线程使用 h_user、h_c2h0 或 h_h2c0。
 */
void pcie_deinit()
{
    user_start_en = 0x00000000;
    write_device(h_user, 0x04, 4, (BYTE *)&user_start_en);//clear irq
    CloseHandle(h_user);
    CloseHandle(h_c2h0);
    CloseHandle(h_h2c0);
}

/**
 * @brief 从 event_0 节点读取一个字节以等待硬件事件。
 * @return read_device 的状态码。
 */
int wait_for_event0()
{
    BYTE val;

    return  read_device(h_event0, 0, 1, (BYTE *)&val);



}

/**
 * @brief 枚举 XDMA 设备并初始化 user、H2C、C2H 和 event_0 通道。
 *
 * 成功后分配两个 4 KiB 对齐、MAX_BYTES_PER_TRANSFER 大小的 DMA 临时缓冲，
 * 并写入控制寄存器以启用中断/启动状态。
 * @return 1 成功；任一步骤失败返回 -1。
 */
int pcie_init()
{
    char user_name[] = "\\user";
    char c2h0_name[] = "\\c2h_0";
    char h2c0_name[] = "\\h2c_0";

    int res = 1;
    fbuf=0;

    DWORD num_devices = get_devices(GUID_DEVINTERFACE_XDMA, base_path, sizeof(base_path));
    verbose_msg("Devices found: %d\n", num_devices);
    if (num_devices < 1)
    {
        printf("error\n");
        return -1;
    }

    h_user = open_devices(base_path, user_name, GENERIC_READ | GENERIC_WRITE);
    if (h_user == INVALID_HANDLE_VALUE) return -1;

    h_h2c0 = open_devices(base_path, h2c0_name,  GENERIC_WRITE);
    if (h_h2c0 == INVALID_HANDLE_VALUE) return -1;

    h_c2h0 = open_devices(base_path, c2h0_name, GENERIC_READ );
    if (h_c2h0 == INVALID_HANDLE_VALUE) return -1;


    h2c_align_mem_tmp = allocate_buffer(MAX_BYTES_PER_TRANSFER,4096);
    c2h_align_mem_tmp = allocate_buffer(MAX_BYTES_PER_TRANSFER,4096);

    if(NULL == h2c_align_mem_tmp || NULL == c2h_align_mem_tmp) return -1;

    open_event();
    user_start_en = 0xffff0000;
    write_device(h_user, 0x04, 4, (BYTE *)&user_start_en);//start irq
    return res;
}


#ifdef __cplusplus
}
#endif
