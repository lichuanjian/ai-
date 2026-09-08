#ifndef PCIE_FUNC_H
#define PCIE_FUNC_H
#ifdef __cplusplus
extern "C" {
#endif
extern unsigned char *h2c_align_mem_tmp;
extern unsigned char *c2h_align_mem_tmp;

/** @brief 经 PCIe H2C 通道向设备地址写入 size 字节，0 表示成功。 */
int h2c_transfer(unsigned int address,unsigned int size,unsigned char *buffer);
/** @brief 经 PCIe C2H 通道从设备地址读取 size 字节，0 表示成功。 */
int c2h_transfer(unsigned int address,unsigned int size,unsigned char *buffer);

/** @brief 读取用户 BAR/寄存器空间的字节块。 */
void user_read(unsigned int address,unsigned int size,unsigned char *buffer);
/** @brief 向用户 BAR/寄存器空间写入字节块。 */
void user_write(unsigned int address,unsigned int size,unsigned char *buffer);
/** @brief 读取控制寄存器的 32 位值。 */
void read_control(long addr,uint32_t *val);
/** @brief 写入控制寄存器的 32 位值。 */
void write_control(long addr,uint32_t val);
/** @brief 打开/注册设备事件资源。 */
int open_event();
/** @brief 阻塞等待事件 0 触发。 */
int wait_for_event0();
/** @brief 关闭 PCIe 句柄并释放底层资源。 */
void pcie_deinit();
/** @brief 初始化 PCIe 设备与控制通道，负值表示失败。 */
int pcie_init();
#ifdef __cplusplus
}
#endif
#endif

