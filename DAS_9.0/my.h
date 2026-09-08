#ifndef MY_H
#define MY_H
/**
 * @brief 由 CUDA 实现的向量逐元素求和接口。
 * @param h_A 左输入向量。
 * @param h_B 右输入向量。
 * @param h_C 输出向量，容量至少为 numElements。
 * @param numElements 元素数量。
 */
void vectorAdd(const float *h_A, const float *h_B, float *h_C, int numElements);
#endif // MY_H
