#pragma once

#if defined(RUNTIMEBOE18NBSDKETDX_EXPORTS)
#   define DLLEXPORT  __declspec(dllexport)
#else
#define DLLEXPORT   __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C"
{
#endif
    enum DIInterMode {
        DILR = 0,
        DIRL = 1,
        DITB = 2,
        DIBT = 3
    };

//功能: 建立与runtime的连接，并初始化各类参数，进程全生命周期内只需调用一次
//input:
//    logFilePath：日志目录路径
//    loglevel: 日志级别 0-关闭， 1-错误，2-调试，默认为0-关闭
// output：
//    instID:用于区分交织初始化的对象实例ID，调用BOE_ETDI_runDI时须作为参数传入
//	返回值：0-失败，1-成功
DLLEXPORT int BOE_ETDX_initSDK(const char* logFilePath = "./",int loglevel = 0);

//功能: 释放SDK
DLLEXPORT void BOE_ETDX_releaseSDK();

//功能: 初始化Dx11,确保调用前已调用BOE_ETDX_initSDK并返回成功，如果返回失败请在收到runtime上线的回调通知之后再调用此接口,
//      当需要改变输出分辨率需重新调用此接口
//input:
//    device:           ID3D11Device*  D3D设备指针
//    viewportTopleftX: 视口左上角X坐标
//    viewportTopleftY: 视口左上角Y坐标
//    viewportWidth:    输出图像宽
//    viewportHeight:   输出图像高
//    DXGI_FORMAT:     输出图像颜色格式 与DXGI_FORMAT枚举对应，仅支持以下格式
//                       DXGI_FORMAT_R10G10B10A2_UNORM   24
//                       DXGI_FORMAT_R8G8B8A8_UNORM      28
//                       DXGI_FORMAT_R8G8B8A8_UNORM_SRGB 29
//                       DXGI_FORMAT_B8G8R8A8_UNORM      87
//                       DXGI_FORMAT_B8G8R8A8_UNORM_SRGB 91
//    posPredicatDelay:眼追预测算法的延迟参数，单位毫秒，与渲染耗时正相关，根据最终效果微调（经验值）,传0则关闭预测
//返回值：0-失败，1-成功
DLLEXPORT int BOE_ETDX_initDx11(void* device,
    int viewportTopleftX =0, int viewportTopleftY = 0,int viewportWidth = 3840, int viewportHeight = 2160,
    int outDXGI_FORMAT = 87, int posPredicatDelay = 0);


//功能: 释放Dx11
DLLEXPORT void BOE_ETDX_releaseDx11();

//功能:dx11版本的交织接口，根据传入的纹理inShaderResourceView将交织结果输出到renderTargetView（如果renderTargetView传空，则输出到context绑定的RTV）
//input:
//    renderTargetView: ID3D11RenderTargetView*  D3D渲染目标视图
//    context:          ID3D11DeviceContext*  D3D设备上下文
//   inShaderResourceView: void**  ID3D11ShaderResourceView** D3D资源视图
//   width:      inShaderResourceView的宽
//   height:     inShaderResourceView的高
//   DXGI_FORMAT:inShaderResourceView的颜色格式，与DXGI_FORMAT枚举对应
//   intermode: 交织方式：0-左右交织  1-右左交织  2-上下交织  3-下上交织
//   sRGBFix: srgb修正，默认0-不修正，1、2表示从线性到sRGB修正1、2次，-1、-2表示从sRGB到线性修正1、2次，参数范围为-2<=sRGBFix<=2
//返回值：0-失败，1-成功
DLLEXPORT int BOE_ETDX_doInterlaceDx11(void* context, void* renderTargetView, void** inShaderResourceView, int width = 3840, int height = 2160,
    int inDXGI_FORMAT = 87,DIInterMode inter_mode = DILR,int sRGBFix = 0);

//功能: 初始化Dx12,确保调用前已调用BOE_ETDX_initSDK并返回成功，如果返回失败请在收到runtime上线的回调通知之后再调用此接口,
//      当需要改变输出分辨率需重新调用此接口
//input:
//    device:           ID3D12Device*  D3D设备指针
//    resource:         ID3D12Resource* 输入纹理的指针
//    viewportTopleftX: 视口左上角X坐标
//    viewportTopleftY: 视口左上角Y坐标
//    viewportWidth:    输出图像宽
//    viewportHeight:   输出图像高
//    DXGI_FORMAT:     输出图像颜色格式 与DXGI_FORMAT枚举对应，仅支持以下格式
//                       DXGI_FORMAT_R10G10B10A2_UNORM   24
//                       DXGI_FORMAT_R8G8B8A8_UNORM      285
//                       DXGI_FORMAT_R8G8B8A8_UNORM_SRGB 29
//                       DXGI_FORMAT_B8G8R8A8_UNORM      87
//                       DXGI_FORMAT_B8G8R8A8_UNORM_SRGB 91
//    posPredicatDelay:眼追预测算法的延迟参数，单位毫秒，与渲染耗时正相关，根据最终效果微调（经验值）,传0则关闭预测
//返回值：0-失败，1-成功
DLLEXPORT int BOE_ETDX_initDx12(void* device,void* resource,
    int viewportTopleftX = 0, int viewportTopleftY = 0, int viewportWidth = 3840, int viewportHeight = 2160,
    int outDXGI_FORMAT = 87, int posPredicatDelay = 0);
//功能: 释放Dx12
DLLEXPORT void BOE_ETDX_releaseDx12();
//功能:dx12版本的交织接口
//input:
//    commandList:       ID3D12GraphicsCommandList*        dx12命令队列
//    renderTargetView:  ID3D12Resource*                   交换链中获取的rtv
//   rtvhandle:          D3DX12_CPU_DESCRIPTOR_HANDLE*    rtv的描述符堆的寄存器索引句柄
//   width:              srv的宽
//   height:             srv的高
//   DXGI_FORMAT:inShaderResourceView的颜色格式，与DXGI_FORMAT枚举对应
//   intermode: 交织方式：0-左右交织  1-右左交织  2-上下交织  3-下上交织
//返回值：0-失败，1-成功
DLLEXPORT int BOE_ETDX_doInterlaceDx12(void* commandList, void* renderTargetView, void* rtvhandle, int width = 3840, int height = 2160,
    int inDXGI_FORMAT = 87, DIInterMode inter_mode = DILR);

//回调函数 功能：sdk主动推送消息给上层应用
//input:
//  type:0-眼追坐标有效，可用时取points数组3x3坐标
//       1-2D/3D切换状态，status参数获取状态
//       2-眼追无效，眼追算法是否捕捉到有效人脸
//       3-runtime服务状态，status参数获取状态
//       4-摄像头状态，status参数获取状态
//       5-串口状态，status参数获取状态
//       7-显示器状态，status参数获取当前3D显示器索引
//  status：当type为1时，0-2D，1-3D, 2-无法开启3D，HSR占用, 3-无法开启3D，串口异常
//		    当type为3时，0-服务关闭，1-服务打开，2-其他异常掉线
//			当type为4时，0-摄像头离线，1-摄像头打开，2-摄像头关闭，3-摄像头占用
//			当type为5时，0-串口离线，1-串口打开，2-串口关闭，3-串口占用
//			当type为7时，status>=0表示3D屏幕索引，-1表示无3D屏幕
// 
//  points：当type = 0时，眼追坐标有效，size为9个float长度，
//          points[0] - [2]为左眼世界坐标x、y、z
//          points[3] - [5]为右眼世界坐标x、y、z
//          points[6] - [8]为眉心坐标x、y、z
//  msg:通知消息字符串
typedef int (*StatusCallBack)(int type, int status, float* points, const char* msg);

//设置回调函数
DLLEXPORT void BOE_ETDX_setCallback(StatusCallBack callBack);

//功能: 关闭眼追及3D
DLLEXPORT void BOE_ETDX_stopDI();

//功能: 开启眼追
//input:
//    debug：是否开启调试模式，默认不开启，开启后会开窗显示摄像头画面
DLLEXPORT void BOE_ETDX_openET(bool debug = false);
 
//功能: 关闭眼追
DLLEXPORT void BOE_ETDX_closeET();

//设置3D模式
//input：
//    open:true-3D,false-2D 
//返回值：0-失败，1-成功 
DLLEXPORT int BOE_ETDX_set3DMode(bool open);

//查看当前是否是3D
//返回值：0-2D,1-3D 
DLLEXPORT int BOE_ETDX_get3DMode();

//功能：获取眼追左右眼坐标，
//输入：points 左右眼和世界坐标float指针,为9个float元素的数组指针,需自行初始化
//输出：points 左右眼和世界坐标
//返回值：0-失败，1-成功 
//失败常见问题：未调用BOE_ET_openEyeTrack开启眼追    
//              points指针为空
DLLEXPORT int BOE_ETDX_getEyePoints(float* points);

//功能: 设置日志模式/等级
//input: 0-关闭，1-错误，2-调试
DLLEXPORT void BOE_ETDX_setLogMode(int logLevel);

//功能: 设置眼追预测延迟
//input: predictDelay-延迟值，单位毫秒
DLLEXPORT void BOE_ETDX_setPredictDelay(int predictDelay);

//功能: 设置瞳距参数
//input: pupillaryVal-瞳距值
DLLEXPORT void BOE_ETDX_setPupillaryDistance(float pupillaryVal);

//功能: 获取当前瞳距值
//input: value  float*类型，调用者分配内存  用于接收瞳距值
//返回值：0-失败，1-成功 

DLLEXPORT int BOE_ETDX_getPupillaryDistance(float* value);

//功能: 获取当前3D监视器索引
//返回值：-1-失败，大于等于0-成功，且返回值为3D监视器索引号 
DLLEXPORT int BOE_ETDX_get3DMonitorIndex();

//功能: 获取当前3D监视器信息，包括索引，监视器区域
// input:
//    index: 3d监视器索引  -1表示不存在3D监视器，大于等于0表示3D监视器索引号   调用者初始化内存
//    left:  3d屏幕rect的left    调用者初始化内存
//    right: 3d屏幕rect的right   调用者初始化内存
//    top:   3d屏幕rect的top     调用者初始化内存
//    bottom:3d屏幕rect的bottom  调用者初始化内存
//返回值：0-失败，1-成功
DLLEXPORT int BOE_ETDX_get3DMonitorInfo(int* index, int* left, int* right, int* top, int* bottom);

#ifdef __cplusplus
}
#endif
