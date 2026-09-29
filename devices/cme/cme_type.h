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

#ifndef __CME_TYPE_H
#define __CME_TYPE_H

#define CME_MAX_PLANE_NUM      3
#define CME_MAX_JOBS           32
#define CME_MAX_TASKS_PER_JOB  10

typedef int cme_job_handle;
typedef int cme_task_handle;

typedef enum {
    CME_RET_SUCCESS           =  0,
    CME_RET_NOT_SUPPORTED     = -1,
    CME_RET_OUT_OF_MEMORY     = -2,
    CME_RET_INVALID_PARAM     = -3,
    CME_RET_BUSY              = -4,
    CME_RET_DEVICE_ERROR      = -5,
    CME_RET_NOT_RUNNING       = -6,
    CME_RET_FAILED            = -7,
    CME_RET_WORKER_EXIT       = -8,
} CME_RET;

typedef enum {
    CME_FORMAT_RGBA_8888= 0,        /* [0:31] R:G:B:A 8:8:8:8 little endian */
    CME_FORMAT_ARGB_8888,           /* [0:31] A:R:G:B 8:8:8:8 little endian */
    CME_FORMAT_RGB_888,             /* [0:23] R:G:B 8:8:8 little endian */
    CME_FORMAT_BGRA_8888,           /* [0:31] B:G:R:A 8:8:8:8 little endian */
    CME_FORMAT_ABGR_8888,           /* [0:31] A:B:G:R 8:8:8:8 little endian */
    CME_FORMAT_BGR_888,             /* [0:23] B:G:R 8:8:8 little endian */

    CME_FORMAT_NV12,                /* 2 plane YUV little endian
                                         * plane 0: [0:7] Y
                                         * plane 1: 2x2 subsampled [0:15] U:V 8:8 */
    CME_FORMAT_NV21,                /* 2 plane YUV little endian
                                         * plane 0: [0:7] Y
                                         * plane 1: 2x2 subsampled [0:15] V:U 8:8 */
    CME_FORMAT_I420,                /* 3 plane YUV little endian
                                         * plane 0: [0:7] Y
                                         * plane 1: 2x2 subsampled [0:7] U
                                         * plane 2: 2x2 subsampled [0:7] V */
    CME_FORMAT_YUYV_422,            /* [0:31] Y0:Cb0:Y1:cr0 8:8:8:8 little endian */

    CME_FORMAT_P010,                /* 2 plane YUV little endian
                                             * plane 0: [0:9] Y
                                             * plane 1: 2x2 subsampled [0:19] U:V 10: 10 (default)
                                             * or
                                             * plane 1: 2x2 subsampled [0:23] U:V 16: 16 */
    CME_FORMAT_RGB_FLOAT,            /* little endian,rgb,data type is 32bit float */
    CME_FORMAT_COMPRESSED,                /* 1 plane compressed stream data,
                                             for example, h264,hevc,jepg...*/
    CME_FORMAT_FLOAT16_VECTOR,       /* 1 plane float16 vector for neural network output
                                             * width = vector dimension, height = 1
                                             * data type is float16 (2 bytes per element) */
    CME_FORMAT_FLOAT32_VECTOR,       /* 1 plane float32 vector for neural network output
                                             * width = vector dimension, height = 1
                                             * data type is float32 (4 bytes per element) */
    CME_FORMAT_INT8_VECTOR,          /* 1 plane int8 vector for neural network output
                                             * width = vector dimension, height = 1
                                             * data type is int8/signed char (1 byte per element) */
    CME_FORMAT_RGB_NCHW_S8,          /* 3 plane RGB, NCHW layout, signed 8-bit per channel
                                             * plane 0: R channel (int8)
                                             * plane 1: G channel (int8)
                                             * plane 2: B channel (int8) */
    CME_FORMAT_RGB_NCHW_U8,          /* 3 plane RGB, NCHW layout, unsigned 8-bit per channel
                                             * plane 0: R channel (uint8)
                                             * plane 1: G channel (uint8)
                                             * plane 2: B channel (uint8) */
    CME_FORMAT_NV12M,                /* multi-plane NV12, each plane has independent dmabuf fd
                                             * plane 0: Y (separate dmabuf)
                                             * plane 1: UV interleaved (separate dmabuf) */
    CME_FORMAT_NV21M,                /* multi-plane NV21, each plane has independent dmabuf fd
                                             * plane 0: Y (separate dmabuf)
                                             * plane 1: VU interleaved (separate dmabuf) */
    CME_FORMAT_I420M,                /* multi-plane I420, each plane has independent dmabuf fd
                                             * plane 0: Y (separate dmabuf)
                                             * plane 1: U (separate dmabuf)
                                             * plane 2: V (separate dmabuf) */
    CME_FORMAT_P010M,                /* multi-plane P010, each plane has independent dmabuf fd
                                             * plane 0: Y (separate dmabuf, 16-bit)
                                             * plane 1: UV interleaved (separate dmabuf, 32-bit) */
    CME_FORMAT_R8,                   /* 1 plane 8-bit grayscale (1 byte per pixel) */
    CME_FORMAT_INT16_VECTOR,         /* 1 plane int16 vector for neural network output
                                             * width = vector dimension, height = 1
                                             * data type is int16/short (2 bytes per element) */
    CME_FORMAT_RGB_NCHW_S16,         /* 3 plane RGB, NCHW layout, signed 16-bit per channel
                                             * plane 0: R channel (int16)
                                             * plane 1: G channel (int16)
                                             * plane 2: B channel (int16) */
    CME_FORMAT_UINT16_VECTOR,        /* 1 plane uint16 vector for neural network input
                                             * width = vector dimension, height = 1
                                             * data type is uint16/unsigned short (2 bytes per element) */
    CME_FORMAT_UINT8_VECTOR,         /* 1 plane uint8 vector for neural network output
                                             * width = vector dimension, height = 1
                                             * data type is uint8/unsigned char (1 byte per element) */
    CME_FORMAT_UNKNOWN,
    CME_FORMAT_MAX = CME_FORMAT_UNKNOWN
} IMG_FORMAT;

typedef enum {
    CME_COLOR_SPACE_DEFAULT = 0,
    CME_CS_BT601_LIMIT,     /* colorspace is BT601, limit range */
    CME_CS_BT709_LIMIT,         /* colorspace is BT709, limit range */
    CME_CS_BT2020_LIMIT,         /* colorspace is BT2020, limit range */
    CME_CS_BT601_FULL,          /* colorspace is BT601, full range */
    CME_CS_BT709_FULL,          /* colorspace is BT709, full range */
    CME_CS_BT2020_FULL,          /* colorspace is BT2020, full range */
} CME_COLOR_SPACE_MODE;

typedef enum {
    CME_HAL_TRANSFORM_FLIP_NONE = 0,
    CME_HAL_TRANSFORM_FLIP_H,       /* horizontal flip */
    CME_HAL_TRANSFORM_FLIP_V,           /* vertical flip */
    CME_HAL_TRANSFORM_FLIP_H_V,         /* horizontal and vertical flip (central flip) */
} CME_FLIP_MODE;

/* Rectangle structure for drawing */
typedef struct {
    int x;          /* Top-left corner X coordinate */
    int y;          /* Top-left corner Y coordinate */
    int width;      /* Rectangle width */
    int height;     /* Rectangle height */
} cme_rect_t;

typedef struct {
    int max_img_width;
    int max_img_height;
    int min_img_width;
    int min_img_height;

    IMG_FORMAT supported_input_formats[32];
    IMG_FORMAT supported_output_formats[32];
} cme_capacity;

typedef enum {
    CME_HWACCEL_MODE_PERFORMANCE = 0,     /* high performace mode*/
    CME_HWACCEL_MODE_ENERGY_EFFICIENCY,   /* high energy efficiency mode */
    CME_HWACCEL_MODE_LOW_POWER,           /* low power mode */
    CME_HWACCEL_MODE_CPU_AVOIDANT,        /* avoid to use cpu */
    CME_HWACCEL_MODE_MAX,
} CME_HWACCEL_MODE;

typedef struct {
    float multi_factor;
    float add_factor;
    float max;
    float min;
    int enable_clamp;
}cme_normalize_ctrl;

typedef struct {
    float multi_factor;
    float add_factor;
    float max;
    float min;
    int enable_clamp;
    int reserved[3];
} cme_normalize_channel_ctrl;

typedef struct {
    int vrow;
    int vcol;
    int enable;
} cme_normalize_transpose_ctrl;

typedef struct {
    cme_normalize_channel_ctrl r;
    cme_normalize_channel_ctrl g;
    cme_normalize_channel_ctrl b;
    cme_normalize_transpose_ctrl transpose;
    void *reserved[36];
}cme_normalize_ctrl_ext;

typedef enum {
    CME_BORDER_CONSTANT = 0, /* fix value fill boarder */
    CME_BORDER_MAX
} CME_BORDER_MODE;

typedef enum {
    CME_ROTATE_0 = 0,
    CME_ROTATE_90,
    CME_ROTATE_180,
    CME_ROTATE_270,
} CME_ROTATE_MODE;

typedef struct {
    int enable_crop;
    int crop_x;
    int crop_y;
    int crop_w;
    int crop_h;
    CME_ROTATE_MODE rotate;
    CME_FLIP_MODE flip_mode;
} cme_crfrc_ctrl;

typedef struct {
    int enable_crop;
    int crop_x;
    int crop_y;
    int crop_w;
    int crop_h;
    CME_ROTATE_MODE rotate;
    CME_FLIP_MODE flip_mode;
    int enable_letterbox;
    int resize_w;
    int resize_h;
    int pad_x;
    int pad_y;
    int fill_value;
    int reserved[8];
} cme_crfrc_ctrl_ext;

typedef struct {
    float tranform_matrix[2][3]; /*  a b tx 
                                     c d ty */
    int border_mode;              /* CME_BORDER_MODE */
    unsigned char border_value[3]; /* R/G/B */
} cme_affine_transform_ctrl;

#define MAX_EST_POINTS 10
#define MIN_EST_POINTS 3

typedef struct {
    int src_point_x[MAX_EST_POINTS];
    int src_point_y[MAX_EST_POINTS];
    int dst_point_x[MAX_EST_POINTS];
    int dst_point_y[MAX_EST_POINTS];
    int point_number;
} cme_est_sim_tfm_ctrl;

typedef struct {
    float a;  /*  a -b tx */
    float b;  /*  b  a ty */
    float tx;
    float ty;
} cme_est_sim_tfm_ret;

typedef struct {
    float *map_x;
    float *map_y;
    float scale;
    int en_linear_sample;
    int en_sharpen;
    int en_5x5_kernel;
    float amount;
} cme_fisheye_remap_ctrl;

typedef struct {
    float *box_encoding;     /* [N, 4], encoded box offsets [dy, dx, dh, dw] */
    float *class_prediction; /* [N, num_classes+1], class confidence scores */
    float *anchors;          /* [N, 4], anchor boxes [y_center, x_center, h, w] */
    int num_anchors;
    int num_classes;
    float scale_y;
    float scale_x;
    float scale_h;
    float scale_w;
    float nms_score_threshold;
    float iou_threshold;
    int max_detections;
    int max_classes_per_detection;
    int use_regular_nms;
    int detection_per_class;
} cme_detection_post_process_ctrl;

typedef struct {
    float *output_boxes;   /* [max_detections, 4], boxes in [ymin, xmin, ymax, xmax] */
    float *output_classes; /* [max_detections], class IDs */
    float *output_scores;  /* [max_detections], confidence scores */
    int   *num_detections;
    int max_detections;
} cme_detection_post_process_ret;

/* NMSBoxes: compatible with OpenCV cv::dnn::NMSBoxes API */
typedef struct {
    float *bboxes;           /* [x, y, w, h] for each box, length = num_boxes * 4 */
    float *scores;           /* confidence scores, length = num_boxes */
    int    num_boxes;        /* total number of candidate boxes */
    float  score_threshold;  /* filter boxes below this score */
    float  nms_threshold;    /* IoU threshold for suppression */
    int    top_k;            /* max boxes to keep, 0 = no limit */
} cme_nms_boxes_ctrl;

typedef struct {
    int *indices;            /* output: indices of kept boxes */
    int  num_output;         /* output: number of kept boxes */
} cme_nms_boxes_ret;

static inline int is_yuv_format(IMG_FORMAT format)
{
    if (format == CME_FORMAT_NV12
        || format == CME_FORMAT_NV21
        || format == CME_FORMAT_I420
        || format == CME_FORMAT_YUYV_422
        || format == CME_FORMAT_P010
        || format == CME_FORMAT_NV12M
        || format == CME_FORMAT_NV21M
        || format == CME_FORMAT_I420M
        || format == CME_FORMAT_P010M)
        return 1;

    return 0;
}

static inline int is_vector_format(IMG_FORMAT format)
{
    if (format == CME_FORMAT_FLOAT16_VECTOR
        || format == CME_FORMAT_FLOAT32_VECTOR
        || format == CME_FORMAT_INT8_VECTOR
        || format == CME_FORMAT_INT16_VECTOR
        || format == CME_FORMAT_UINT16_VECTOR
        || format == CME_FORMAT_UINT8_VECTOR)
    return 1;

    return 0;
}

#endif