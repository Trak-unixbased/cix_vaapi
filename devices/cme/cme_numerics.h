/*
* Copyright 2026 Cix Technology Group Co., Ltd.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef __CME_NUMERICS_H
#define __CME_NUMERICS_H

#include "cme_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Find the maximum value in a float32 array using NEON SIMD.
 *
 * @param data    Input float array (must be non-NULL)
 * @param n       Number of elements (must be >= 1)
 * @param max_val Output: maximum value found
 * @return        CME_RET_SUCCESS on success, CME_RET_INVALID_PARAM on error
 */
CME_RET cme_argmax_val(const float *data, int n, float *max_val);

/**
 * NEON-accelerated sigmoid: dst[i] = 1/(1+exp(-src[i]))
 * Uses Schraudolph fast exp (~3% max error). Processes 4 floats at a time.
 *
 * @param src  Input float array
 * @param dst  Output float array (same size as src)
 * @param n    Number of elements
 * @return     CME_RET_SUCCESS or CME_RET_INVALID_PARAM
 */
CME_RET cme_sigmoid(const float *src, float *dst, int n);

/* GEMM flags (OpenCV cv::gemm compatible) */
#define CME_GEMM_1_T  1   /* transpose src1 (A) */
#define CME_GEMM_2_T  2   /* transpose src2 (B) */
#define CME_GEMM_3_T  4   /* transpose src3 (C) */

/**
 * General Matrix Multiply (NEON accelerated, float32).
 * D = alpha * op(A) * op(B) + beta * op(C)
 *
 * @param src1  Matrix A (row-major, or column-major if CME_GEMM_1_T)
 * @param src2  Matrix B (row-major, or column-major if CME_GEMM_2_T)
 * @param alpha Scale factor for A*B
 * @param src3  Matrix C, or NULL to skip the beta*C term
 * @param beta  Scale factor for C (ignored when src3 == NULL)
 * @param dst   Output matrix D (m x n, row-major, or col-major if CME_GEMM_3_T)
 * @param m     Rows of op(A) and D
 * @param n     Columns of op(B) and D
 * @param k     Columns of op(A) / rows of op(B)
 * @param flags Combination of CME_GEMM_*_T flags
 * @return      CME_RET_SUCCESS or error code
 */
CME_RET cme_gemm(const float *src1, const float *src2, float alpha,
                 const float *src3, float beta, float *dst,
                 int m, int n, int k, int flags);

/**
 * Async variant of cme_gemm.
 * When sync=true, blocks until complete.
 * When sync=false, returns immediately with *wait_fd for cme_wait_job().
 */
CME_RET cme_gemm_async(const float *src1, const float *src2, float alpha,
                       const float *src3, float beta, float *dst,
                       int m, int n, int k, int flags,
                       bool sync, int *wait_fd);

/**
 * Task composition variant of cme_gemm.
 * Adds a GEMM task to an existing job handle (from cme_begin_task).
 */
CME_RET cme_gemm_task(cme_job_handle handle,
                      const float *src1, const float *src2, float alpha,
                      const float *src3, float beta, float *dst,
                      int m, int n, int k, int flags);

/**
 * NEON-accelerated matrix transpose (float32).
 * Transposes a rows x cols matrix to a cols x rows matrix.
 *
 * @param src   Input matrix (rows x cols, row-major)
 * @param dst   Output matrix (cols x rows, row-major)
 * @param rows  Number of rows in src
 * @param cols  Number of columns in src
 * @return      CME_RET_SUCCESS or CME_RET_INVALID_PARAM
 */
CME_RET cme_transpose(const float *src, float *dst, int rows, int cols);

/**
 * Async variant of cme_transpose.
 */
CME_RET cme_transpose_async(const float *src, float *dst, int rows, int cols,
                            bool sync, int *wait_fd);

/**
 * Task composition variant of cme_transpose.
 */
CME_RET cme_transpose_task(cme_job_handle handle,
                           const float *src, float *dst, int rows, int cols);

#ifdef __cplusplus
}
#endif

#endif /* __CME_NUMERICS_H */
