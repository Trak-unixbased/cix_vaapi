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

#ifndef __CME_2D_H
#define __CME_2D_H


#ifdef __cplusplus
extern "C" {
#endif

#define MAX_LAYERS  4

/**
 * Initialize libcme resources (OpenCL context, thread pools).
 *
 * This function pre-initializes OpenCL and thread pools. If not called,
 * resources are lazily initialized on first use.
 *
 * @returns CME_RET_SUCCESS on success, or error code on failure.
 */
CME_RET cme_initialize(void);

/**
 * Release all libcme resources (OpenCL context, kernels, thread pools).
 *
 * After calling this, subsequent API calls will reinitialize resources
 * lazily. Safe to call multiple times.
 *
 * @returns CME_RET_SUCCESS on success.
 */
CME_RET cme_destroy(void);

 /**
 * image resize
 *
 * @param src_img
 *      The input source image.
 * @param dst_img
 *      The output destination image.
 * @param sync
 *      true: sync mode,false: async mode
 * @param wait_fd
 *      used for cme_wait_job
 * @returns
 *       success or else negative error code.
 */
CME_RET cme_2d_resize(cme_img *src_img, cme_img *dst_img, bool sync,int *wait_fd);

 /**
 * image colorspace conversion
 *
 * @param src_img
 *      The input source image.
 * @param dst_img
 *      The output destination image.
 * @param mode
 *      CME_YUV_TO_RGB_BT601_LIMIT
 *      CME_YUV_TO_RGB_BT601_FULL
 *      CME_YUV_TO_RGB_BT709_LIMIT
 *      CME_YUV_TO_RGB_MASK
 *      CME_RGB_TO_YUV_BT601_FULL
 *      CME_RGB_TO_YUV_BT601_LIMIT
 *      CME_RGB_TO_YUV_BT709_LIMIT
 * @param sync
 *      true: sync mode,false: async mode
 * @param wait_fd
 *      used for cme_wait_job
 * @returns success or else negative error code.
 */
CME_RET cme_2d_cvtcolor(cme_img *src_img, cme_img *dst_img, int mode, bool sync, int *wait_fd);

 /**
 * images overlay
 *
 * @param nimgs
 *      The number of input images,max number is 4
 * @param src_imgs
 *      The array of input source images.
 * @param dst_imgs
 *      The output destination image.
 * @param alpha
 *       value 0~65535,65535 is no transparent,or not fill
 * @param sync
 *      true: sync mode,false: async mode
 * @param wait_fd
 *      used for cme_wait_job
 * @returns success or else negative error code.
 * note: src_img[0] locate on bottom level
 */
CME_RET cme_2d_overlay(int nimgs, cme_img **src_imgs, cme_img *dst_img, int *alpha, bool sync,int *wait_fd);

 /**
 * image copy
 *
 * @param src_img
 *      The input source image.
 * @param dst_img
 *      The output destination image.
 * @param sync
 *      true: sync mode,false: async mode
 * @param wait_fd
 *      used for cme_wait_job
 * @returns
 *       success or else negative error code.
 */
CME_RET cme_img_copy(cme_img *src_img, cme_img *dst_img, bool sync,int *wait_fd);

 /**
 * image resize->cvtcolor->flip
 *
 * @param src_img
 *      The input source image.
 *      Can support NV12 YUYV RGB888 RGBA888 now
 * @param dst_img
 *      The output destination image.
 *      Only support RGB888 format only now.
 * @param mode
 *      CME_HAL_TRANSFORM_FLIP_H
 *      CME_HAL_TRANSFORM_FLIP_V
 *      CME_HAL_TRANSFORM_FLIP_H_V
 * @param sync
 *      true: sync mode,false: async mode
 * @param wait_fd
 *      used for cme_wait_job
 * @returns success or else negative error code.
 */
CME_RET cme_2d_resize_cvtcolor_and_flip(cme_img *src_img, cme_img *dst_img, int mode, bool sync,int *wait_fd);

 /**
 * image multi and add for each pixel
 *
 * @param src_img
 *      The input source image.
 *      Can support RGB888 or RGB_FLOAT
 * @param dst_img
 *      The output destination image.
 *      Only support RGB_FLOAT format only now.
 * @param ctrl
 *      cme_normalize_ctrl type input parameters
 *      refer to api document
 * @param sync
 *      true: sync mode,false: async mode
 * @param wait_fd
 *      used for cme_wait_job
 * @returns success or else negative error code.
 */
CME_RET cme_2d_normalize(cme_img *src_img, cme_img *dst_img, cme_normalize_ctrl *ctrl, bool sync,int *wait_fd);

CME_RET cme_2d_normalize_ext(cme_img *src_img, cme_img *dst_img, cme_normalize_ctrl_ext *ctrl, bool sync, int *wait_fd);

 /**
 * image affine transform
 *
 * @param src_img
 *      The input source image.
 *      Can support RGB888
 * @param dst_img
 *      The output destination image.
 *      Only support RGB888
 * @param ctrl
 *      cme_affine_transform_ctrl type input parameters
 *      refer to api document
 * @param sync
 *      true: sync mode,false: async mode
 * @param wait_fd
 *      used for cme_wait_job
 * @returns success or else negative error code.
 */
CME_RET cme_2d_affine_transform(cme_img *src_img, cme_img *dst_img, cme_affine_transform_ctrl *ctrl, bool sync,int *wait_fd);

 /**
 * estimate similarity transfrom matrix
 *
 * @param src_img
 *      The input source image.
 *      Can support RGB888
 * @param dst_img
 *      The output destination image.
 *      Only support RGB888
 * @param ctrl
 *      map address and centor crop parameters
 * @param ret
 *      similarity transfrom parameters
 * @param sync
 *      true: sync mode,false: async mode
 * @param wait_fd
 *      used for cme_wait_job
 * @returns success or else negative error code.
 */
CME_RET cme_2d_est_sim_tfm(cme_est_sim_tfm_ctrl *ctrl, cme_est_sim_tfm_ret *ret, bool sync,int *wait_fd);

 /**
 * remap for fisheye camera undistortion
 *
 * @param ctrl
 *      input src & dst points
 * @param ret
 *      similarity transfrom parameters
 * @param sync
 *      true: sync mode,false: async mode
 * @param wait_fd
 *      used for cme_wait_job
 * @returns success or else negative error code.
 */
CME_RET cme_2d_fisheye_remap(cme_img *src_img, cme_img *dst_img, cme_img *tmp_img, cme_fisheye_remap_ctrl *ctrl, bool sync,int *wait_fd);

/**
 * compute cosine similarity between two FLOAT32_VECTOR format images
 *
 * @param src_img_a
 *      The first input vector image (FLOAT32_VECTOR format).
 * @param src_img_b
 *      The second input vector image (FLOAT32_VECTOR format).
 *      Must have same dimension as src_img_a.
 * @param result
 *      Output pointer for the cosine similarity result.
 *      The value will be in range [-1.0, 1.0].
 * @param tmp_img
 *      Optional temporary buffer for partial results (FLOAT32_VECTOR format).
 *      Width must be >= 3 * num_workgroups where num_workgroups = (dim+511) / 512.
 *      If NULL, temporary buffer will be allocated internally.
 * @param sync
 *      true: sync mode,false: async mode
 * @param wait_fd
 *      used for cme_wait_job
 * @returns success or else negative error code.
 */
CME_RET cme_2d_cosine_sim(cme_img *src_img_a, cme_img *src_img_b, float *result, cme_img *tmp_img, bool sync, int *wait_fd);

/**
 * image crop, resize, flip, rotation, colorspace conversion combined operation
 *
 * @param src_img
 *      The input source image.
 * @param dst_img
 *      The output destination image.
 * @param ctrl
 *      cme_crfrc_ctrl type input parameters
 *      - enable_crop: 0 to skip crop, non-zero to enable
 *      - crop_x, crop_y, crop_w, crop_h: crop region
 *      - rotate: CME_ROTATE_0/90/180/270
 *      - flip_mode: CME_HAL_TRANSFORM_FLIP_NONE/H/V/H_V
 * @param sync
 *      true: sync mode, false: async mode
 * @param wait_fd
 *      used for cme_wait_job
 * @returns success or else negative error code.
 */
CME_RET cme_2d_crop_resize_flip_rotation_cvtcolor(cme_img *src_img, cme_img *dst_img, cme_crfrc_ctrl *ctrl, bool sync, int *wait_fd);

/*
 * In the future, we need to consider whether it is necessary to provide the task family of functions.
 * At present, there are no hardware-supported multitasks that can be merged or monitored.
 * This kind of scenario is exactly what GPUs excel at.
 */
/**
 * image resize
 *
 * @param handle
 *      The job handle, add task to one job
 * @param src_img
 *      The input source image.
 * @param dst_img
 *      The output destination image.
 * @returns
 *       success or else negative error code.
 */
CME_RET cme_2d_resize_task(cme_job_handle handle, cme_img *src_img, cme_img *dst_img);

/**
 * image colorspace conversion
 *
 * @param handle
 *      The job handle, add task to one job
 * @param src_img
 *      The input source image.
 * @param dst_img
 *      The output destination image.
 * @param mode
 *      CME_YUV_TO_RGB_BT601_LIMIT
 *      CME_YUV_TO_RGB_BT601_FULL
 *      CME_YUV_TO_RGB_BT709_LIMIT
 *      CME_YUV_TO_RGB_MASK
 *      CME_RGB_TO_YUV_BT601_FULL
 *      CME_RGB_TO_YUV_BT601_LIMIT
 *      CME_RGB_TO_YUV_BT709_LIMIT
 * @returns success or else negative error code.
 */
CME_RET cme_2d_cvtcolor_task(cme_job_handle handle, cme_img *src_img, cme_img *dst_img, int mode);

 /**
 * images overlay
 *
 * @param handle
 *      The job handle, add task to one job
 * @param nimgs
 *      The number of input images,max number is 4
 * @param src_imgs
 *      The array of input source images.
 * @param dst_imgs
 *      The output destination image.
 * @param alpha
 *       value 0~65535,65535 is no transparent,or not fill
 * @param mode
 *      CME_HAL_TRANSFORM_FLIP_H
 *      CME_HAL_TRANSFORM_FLIP_V
 *      CME_HAL_TRANSFORM_FLIP_H_V
 * @returns success or else negative error code.
 */
CME_RET cme_2d_overlay_task(cme_job_handle handle, int src_num, cme_img **src_imgs, int *alpha, cme_img *dst_img);

/**
 * image copy
 *
 * @param handle
 *      The job handle, add task to one job
 * @param src_img
 *      The input source image.
 * @param dst_img
 *      The output destination image.
 * @returns
 *       success or else negative error code.
 */
CME_RET cme_img_copy_task(cme_job_handle handle, cme_img *src_img, cme_img *dst_img);

/**
 * image normalize
 *
 * @param handle
 *      The job handle, add task to one job
 * @param src_img
 *      The input source image.
 * @param dst_img
 *      The output destination image.
 * @param ctrl
 *      cme_normalize_ctrl type input parameters
 * @returns success or else negative error code.
 */
CME_RET cme_2d_normalize_task(cme_job_handle handle, cme_img *src_img, cme_img *dst_img, cme_normalize_ctrl *ctrl);

CME_RET cme_2d_normalize_ext_task(cme_job_handle handle, cme_img *src_img, cme_img *dst_img, cme_normalize_ctrl_ext *ctrl);

/**
 * image crop, resize, flip, rotation, colorspace conversion combined operation
 *
 * @param handle
 *      The job handle, add task to one job
 * @param src_img
 *      The input source image.
 * @param dst_img
 *      The output destination image.
 * @param ctrl
 *      cme_crfrc_ctrl type input parameters
 * @returns success or else negative error code.
 */
CME_RET cme_2d_crop_resize_flip_rotation_cvtcolor_task(cme_job_handle handle, cme_img *src_img, cme_img *dst_img, cme_crfrc_ctrl *ctrl);

/**
 * crop + resize + flip + rotation + color conversion with letterbox support.
 *
 * Extends cme_2d_crop_resize_flip_rotation_cvtcolor with letterbox padding.
 * When enable_letterbox=1 in ctrl_ext, the image is proportionally resized to
 * (resize_w x resize_h) within the dst image, with padding filled at (pad_x, pad_y)
 * offset using fill_value. Only RGB888 output supports letterbox.
 *
 * @param src_img   The input source image.
 * @param dst_img   The output destination image (final letterbox size).
 * @param ctrl_ext  Extended control struct with letterbox fields.
 * @param sync      true: sync mode, false: async mode
 * @param wait_fd   used for cme_wait_job
 * @returns success or else negative error code.
 */
CME_RET cme_2d_crop_resize_flip_rotation_cvtcolor_ext(cme_img *src_img, cme_img *dst_img, cme_crfrc_ctrl_ext *ctrl_ext, bool sync, int *wait_fd);

/**
 * task variant of crop_resize_flip_rotation_cvtcolor_ext for job composition.
 */
CME_RET cme_2d_crop_resize_flip_rotation_cvtcolor_ext_task(cme_job_handle handle, cme_img *src_img, cme_img *dst_img, cme_crfrc_ctrl_ext *ctrl_ext);

/**
 * image resize->cvtcolor->flip combined operation
 *
 * @param handle
 *      The job handle, add task to one job
 * @param src_img
 *      The input source image.
 * @param dst_img
 *      The output destination image.
 * @param mode
 *      CME_HAL_TRANSFORM_FLIP_H/V/H_V
 * @returns success or else negative error code.
 */
CME_RET cme_2d_resize_cvtcolor_and_flip_task(cme_job_handle handle, cme_img *src_img, cme_img *dst_img, int mode);

/**
 * image affine transform
 *
 * @param handle
 *      The job handle, add task to one job
 * @param src_img
 *      The input source image.
 * @param dst_img
 *      The output destination image.
 * @param ctrl
 *      cme_affine_transform_ctrl type input parameters
 * @returns success or else negative error code.
 */
CME_RET cme_2d_affine_transform_task(cme_job_handle handle, cme_img *src_img, cme_img *dst_img, cme_affine_transform_ctrl *ctrl);

/**
 * estimate similarity transform matrix
 *
 * @param handle
 *      The job handle, add task to one job
 * @param ctrl
 *      cme_est_sim_tfm_ctrl type input parameters
 * @param ret
 *      cme_est_sim_tfm_ret type output result
 * @returns success or else negative error code.
 */
CME_RET cme_2d_est_sim_tfm_task(cme_job_handle handle, cme_est_sim_tfm_ctrl *ctrl, cme_est_sim_tfm_ret *ret);

/**
 * remap for fisheye camera undistortion
 *
 * @param handle
 *      The job handle, add task to one job
 * @param src_img
 *      The input source image.
 * @param dst_img
 *      The output destination image.
 * @param tmp_img
 *      Optional temporary buffer for sharpening.
 * @param ctrl
 *      cme_fisheye_remap_ctrl type input parameters
 * @returns success or else negative error code.
 */
CME_RET cme_2d_fisheye_remap_task(cme_job_handle handle, cme_img *src_img, cme_img *dst_img, cme_img *tmp_img, cme_fisheye_remap_ctrl *ctrl);

/**
 * compute cosine similarity between two FLOAT32_VECTOR format images
 *
 * @param handle
 *      The job handle, add task to one job
 * @param src_img_a
 *      The first input vector image.
 * @param src_img_b
 *      The second input vector image.
 * @param result
 *      Output pointer for the cosine similarity result.
 * @param tmp_img
 *      Optional temporary buffer for partial results.
 * @returns success or else negative error code.
 */
CME_RET cme_2d_cosine_sim_task(cme_job_handle handle, cme_img *src_img_a, cme_img *src_img_b, float *result, cme_img *tmp_img);

/**
 * Draw a rectangle on image
 *
 * @param dst_img
 *      The output destination image (RGB888/BGR888 format).
 * @param rect
 *      Rectangle to draw (x, y, width, height).
 * @param color
 *      Color of the rectangle (RGB values, 0-255).
 * @param thickness
 *      Line thickness in pixels.
 * @param sync
 *      true: sync mode, false: async mode.
 * @param wait_fd
 *      Used for cme_wait_job (async mode only).
 * @returns
 *       success or else negative error code.
 */
CME_RET cme_2d_draw_rectangle(cme_img *dst_img,
                               const cme_rect_t *rect,
                               const unsigned char color[3],
                               int thickness,
                               bool sync,
                               int *wait_fd);

/**
 * detection post process (SSD MultiBox decoding + NMS)
 *
 * @param ctrl
 *      The input detection post process control parameters.
 *      refer to api document
 * @param ret
 *      The output detection results.
 * @param sync
 *      true: sync mode,false: async mode
 * @param wait_fd
 *      used for cme_wait_job
 * @returns success or else negative error code.
 */
CME_RET cme_2d_detection_post_process(cme_detection_post_process_ctrl *ctrl,
                                       cme_detection_post_process_ret *ret,
                                       bool sync, int *wait_fd);

/**
 * NMS boxes (compatible with OpenCV cv::dnn::NMSBoxes)
 *
 * Performs non-maximum suppression on a set of decoded bounding boxes.
 * Input boxes are in [x, y, w, h] format (top-left corner + width/height).
 *
 * @param ctrl
 *      The input NMS control parameters (bboxes, scores, thresholds).
 * @param ret
 *      The output indices of kept boxes and count.
 * @param sync
 *      true: sync mode, false: async mode
 * @param wait_fd
 *      used for cme_wait_job
 * @returns success or else negative error code.
 */
CME_RET cme_2d_nms_boxes(cme_nms_boxes_ctrl *ctrl,
                          cme_nms_boxes_ret *ret,
                          bool sync, int *wait_fd);

/**
 * bitwise AND of two RGB888 images, with optional mask and ROI extraction
 *
 * @param src1    First input RGB888 image
 * @param src2    Second input RGB888 image (same size as src1). NULL means use src1.
 * @param dst     Output RGB888 image, width/height may differ from src
 * @param mask    Optional 8-bit single channel mask image (must match dst size). NULL = no mask.
 * @param roi_x   X offset in src to define ROI (default 0)
 * @param roi_y   Y offset in src to define ROI (default 0)
 * @param sync    true: sync mode, false: async mode
 * @param wait_fd used for cme_wait_job
 * @returns CME_RET_SUCCESS or error code
 *
 * Note: ROI width/height equals dst width/height. Mask must match dst dimensions.
 * When src2 == NULL, src2 is treated as src1.
 * When src2 == src1 and mask != NULL, dst = src & mask (ROI copy with masking).
 * When src2 != src1, dst = src1 & src2 (pixel-wise bitwise AND from ROI).
 */
CME_RET cme_2d_bitwise_and(cme_img *src1, cme_img *src2, cme_img *dst,
                           cme_img *mask, int roi_x, int roi_y,
                           bool sync, int *wait_fd);

/**
 * bitwise AND task (for job composition)
 *
 * @param handle  The job handle
 * @param src1    First input RGB888 image
 * @param src2    Second input RGB888 image. NULL means use src1.
 * @param dst     Output RGB888 image
 * @param mask    Optional mask image. NULL = no mask.
 * @param roi_x   X offset in src
 * @param roi_y   Y offset in src
 * @returns CME_RET_SUCCESS or error code
 */
CME_RET cme_2d_bitwise_and_task(cme_job_handle handle, cme_img *src1, cme_img *src2,
                                cme_img *dst, cme_img *mask, int roi_x, int roi_y);

#ifdef __cplusplus
}
#endif

#endif