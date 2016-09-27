
该版本支持AWB,AEC，WDR（预设block模式），支持ov2710 tunning file文件。

1. 只提供库时，需暴露的头文件
HAL/include/
include/shared_ptr.h
include/ebase
include/oslayer

2.需要给dsp模块的头文件

HAL/include/cam_types.h
include/linux/media/rk-isp11-config.h	
include/linux/media/v4l2-config_rockchip.h

  struct HAL_Buffer_MetaData作如下更新：
    1） struct HAL_Buffer_MetaData 信息分两部分，一部分为可能比较关心的模块配置，这部分已转换为
    标准的数据格式；另一部分为当前ISP寄存器级别的配置，需要通过特定转换函数，或者对照
    ISP datasheet才能较好解读。
    video应用需要在得到CameraBuffer后，调用
        HAL_Buffer_MetaData* CameraBuffer:: getMetaData()  获取对应的metadata，然后将指针传给dsp。
     
struct HAL_Buffer_MetaData {
        /* 该帧对应的white balance  gain*/
         struct HAL_ISP_awb_gain_cfg_s    wb_gain;
        /* 该帧对应的filter ：sharp & noise leverl*/
         struct HAL_ISP_flt_cfg_s flt;
        /* 该帧对应的wdr 配置信息，不确定dsp端需要知道什么信息，基本上是把
        当前wdr寄存器信息都列出来了*/
         struct HAL_ISP_wdr_cfg_s wdr;
        /*该帧对应的dpf strength信息*/
         struct HAL_ISP_dpf_strength_cfg_s dpf_strength;
        /*该帧对应的dpf配置信息，该模块寄存器很多，不确定dsp端需要什么信息，
         还未实现*/
         struct HAL_ISP_dpf_cfg_s dpf;
        /*该帧对应的曝光时长及增益*/
         float exp_time;
         float exp_gain;
        /*该帧对应的ISP各模块使能信息*/
         bool_t enabled[HAL_ISP_MODULE_MAX_ID_ID+1];
        /* 对应于 include/linux/media/rk-isp11-config.h 中
        struct v4l2_buffer_metadata_s 结构，ISP各模块
        对应的信息都有，基本是寄存器级别的，需要
        对照ISP datasheet才能解读*/
         void* metedata_drv;
};
 
    2） CamIsp11DevHwItf::configureISPModules(const void* config);
    该函数不由dsp直接调用，dsp需要将配置信息填入struct HAL_ISP_cfg_s结构，将该结构体指针
    给video，video调用CamIsp11DevHwItf::configureISPModules(const void* config)进行配置。
    struct HAL_ISP_cfg_s {
        /* 需要配置的模块都要置上mask位，如 HAL_ISP_WDR_MASK*/
         uint32_t updated_mask;
        /*对应模块的使能信息，updated_mask对应位置上才生效：
            HAL_ISP_ACTIVE_FALSE： 关闭该模块
            HAL_ISP_ACTIVE_SETTING：使用外部配置，该种模式时，需要传入对应的配置
                            信息，如配置wdr模块，那么需要对struct HAL_ISP_wdr_cfg_s *wdr_cfg
                            字段进行赋值。
            HAL_ISP_ACTIVE_DEFAULT：默认配置，由HAL决定其行为，可能从tunning file中
                            取得配置，或者使用默认写死的配置，或者由3A模块控制。每个模块
                            默认都为default配置。
        */
         enum HAL_ISP_ACTIVE_MODE enabled[HAL_ISP_MODULE_MAX_ID_ID+1];
    };
