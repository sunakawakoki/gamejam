#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include <vector>
#include <string>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#define DIRECTINPUT_VERSION     0x0800   // DirectInputのバージョン指定
#include <dinput.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

using namespace DirectX;

#include <DirectXTex.h>

LRESULT WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    // メッセージで分岐
    switch (msg) {
    case WM_DESTROY: // ウィンドウが破棄された
        PostQuitMessage(0); // OSに対して、アプリの終了を伝える
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam); // 標準の処理を行う
}

struct Object3d
{
    ID3D12Resource *constBuff;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandleCBV;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandleCBV;

    XMFLOAT3 scale = { 1,1,1 };
    XMFLOAT3 rotation = {0,0,0};
    XMFLOAT3 position = {0,0,0};

    XMMATRIX matWorld;

    Object3d *parent = nullptr;
};

// 定数バッファ用データ構造体
struct ConstBufferData {
    XMFLOAT4 color; // 色 (RGBA)
    XMMATRIX mat;
};

void InitializeObject3d(Object3d *object,int index,ID3D12Device *dev,ID3D12DescriptorHeap *descHeap)
{
    HRESULT result;

    D3D12_HEAP_PROPERTIES heapprop{};
    heapprop.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resdesc{};
    resdesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resdesc.Width = (sizeof(ConstBufferData) + 0xff) & ~0xff;
    resdesc.Height = 1;
    resdesc.DepthOrArraySize = 1;
    resdesc.MipLevels = 1;
    resdesc.SampleDesc.Count = 1;
    resdesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    result = dev->CreateCommittedResource(
        &heapprop,
        D3D12_HEAP_FLAG_NONE,
        &resdesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&object->constBuff));

    UINT descHandleIncrementSize =
        dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    object->cpuDescHandleCBV = descHeap->GetCPUDescriptorHandleForHeapStart();
    object->cpuDescHandleCBV.ptr += index * descHandleIncrementSize;

    object->gpuDescHandleCBV = descHeap->GetGPUDescriptorHandleForHeapStart();
    object->gpuDescHandleCBV.ptr += index * descHandleIncrementSize;

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
    cbvDesc.BufferLocation = object->constBuff->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = (UINT)object->constBuff->GetDesc().Width;
    dev->CreateConstantBufferView(
        &cbvDesc, object->cpuDescHandleCBV);

}

void UpdateObject3d(Object3d *object,XMMATRIX &matView,XMMATRIX &matProjection)
{
    XMMATRIX matScale, matRot, matTrans;

    matScale = XMMatrixScaling(object->scale.x, object->scale.y, object->scale.z);
    matRot = XMMatrixIdentity();
    matRot *= XMMatrixRotationZ(XMConvertToRadians(object->rotation.z));
    matRot *= XMMatrixRotationX(XMConvertToRadians(object->rotation.x));
    matRot *= XMMatrixRotationY(XMConvertToRadians(object->rotation.y));    
    matTrans = XMMatrixTranslation(object->position.x, object->position.y, object->position.z);

    object->matWorld = XMMatrixIdentity();
    object->matWorld *= matScale;
    object->matWorld *= matRot;
    object->matWorld *= matTrans;

    if (object->parent != nullptr)
    {
        object->matWorld *= object->parent->matWorld;
    }

    ConstBufferData *constMap = nullptr;
    if (SUCCEEDED(object->constBuff->Map(0, nullptr, (void **)&constMap)))
    {
        
        constMap->color = XMFLOAT4(1, 1, 1, 1);
        constMap->mat = object->matWorld * matView * matProjection;
        object->constBuff->Unmap(0, nullptr);
    }

}

void DrawObject3d(Object3d *object, ID3D12GraphicsCommandList *cmdList, ID3D12DescriptorHeap *descHeap, D3D12_VERTEX_BUFFER_VIEW &vbView,
    D3D12_INDEX_BUFFER_VIEW &ibView, D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandleSRV, UINT numIndices)
{
    cmdList->IASetVertexBuffers(0, 1, &vbView);
    cmdList->IASetIndexBuffer(&ibView);

    ID3D12DescriptorHeap *ppHeaps[] = { descHeap };
    cmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
       /* 定数バッファビューをセット*/
    cmdList->SetGraphicsRootDescriptorTable(0, object->gpuDescHandleCBV);
    cmdList->SetGraphicsRootDescriptorTable(1, gpuDescHandleSRV);
    cmdList->DrawIndexedInstanced(numIndices, 1, 0, 0, 0);

}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{

#pragma region WindowsAPI初期化
    // ウィンドウサイズ
    const int window_width = 1280;  // 横幅
    const int window_height = 720;  // 縦幅
    
    const int constantBufferNum = 128;
    const int OBJCT_NUM = 30;

    int counter = 0;

    Object3d object3ds[OBJCT_NUM];

    WNDCLASSEX w{}; // ウィンドウクラスの設定
    w.cbSize = sizeof(WNDCLASSEX);
    w.lpfnWndProc = (WNDPROC)WindowProc; // ウィンドウプロシージャを設定
    w.lpszClassName = L"DirectXGame"; // ウィンドウクラス名
    w.hInstance = GetModuleHandle(nullptr); // ウィンドウハンドル
    w.hCursor = LoadCursor(NULL, IDC_ARROW); // カーソル指定

    // ウィンドウクラスをOSに登録
    RegisterClassEx(&w);
    // ウィンドウサイズ{ X座標 Y座標 横幅 縦幅 }
    RECT wrc = { 0, 0, window_width, window_height };
    AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false); // 自動でサイズ補正

    // ウィンドウオブジェクトの生成
    HWND hwnd = CreateWindow(w.lpszClassName, // クラス名
        L"DirectXGame",         // タイトルバーの文字
        WS_OVERLAPPEDWINDOW,        // 標準的なウィンドウスタイル
        CW_USEDEFAULT,              // 表示X座標（OSに任せる）
        CW_USEDEFAULT,              // 表示Y座標（OSに任せる）
        wrc.right - wrc.left,       // ウィンドウ横幅
        wrc.bottom - wrc.top,   // ウィンドウ縦幅
        nullptr,                // 親ウィンドウハンドル
        nullptr,                // メニューハンドル
        w.hInstance,            // 呼び出しアプリケーションハンドル
        nullptr);               // オプション

    // ウィンドウ表示
    ShowWindow(hwnd, SW_SHOW);

    MSG msg{};  // メッセージ
#pragma endregion WindowsAPI初期化

#pragma region DirectX初期化処理
    // DirectX初期化処理　ここから
#ifdef _DEBUG
//デバッグレイヤーをオンに
    ID3D12Debug *debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
    }
#endif

    HRESULT result;
    ID3D12Device *dev = nullptr;
    IDXGIFactory6 *dxgiFactory = nullptr;
    IDXGISwapChain4 *swapchain = nullptr;
    ID3D12CommandAllocator *cmdAllocator = nullptr;
    ID3D12GraphicsCommandList *cmdList = nullptr;
    ID3D12CommandQueue *cmdQueue = nullptr;
    ID3D12DescriptorHeap *rtvHeaps = nullptr;

    // DXGIファクトリーの生成
    result = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
    // アダプターの列挙用
    std::vector<IDXGIAdapter1 *> adapters;
    // ここに特定の名前を持つアダプターオブジェクトが入る
    IDXGIAdapter1 *tmpAdapter = nullptr;
    for (int i = 0;
        dxgiFactory->EnumAdapters1(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND;
        i++)
    {
        adapters.push_back(tmpAdapter); // 動的配列に追加する
    }

    for (int i = 0; i < adapters.size(); i++)
    {
        DXGI_ADAPTER_DESC1 adesc;
        adapters[i]->GetDesc1(&adesc);  // アダプターの情報を取得

        // ソフトウェアデバイスを回避
        if (adesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        std::wstring strDesc = adesc.Description;   // アダプター名
        // Intel UHD Graphics（オンボードグラフィック）を回避
        if (strDesc.find(L"Intel") == std::wstring::npos)
        {
            tmpAdapter = adapters[i];   // 採用
            break;
        }
    }

    // 対応レベルの配列
    D3D_FEATURE_LEVEL levels[] =
    {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    D3D_FEATURE_LEVEL featureLevel;

    for (int i = 0; i < _countof(levels); i++)
    {
        // 採用したアダプターでデバイスを生成
        result = D3D12CreateDevice(tmpAdapter, levels[i], IID_PPV_ARGS(&dev));
        if (result == S_OK)
        {
            // デバイスを生成できた時点でループを抜ける
            featureLevel = levels[i];
            break;
        }
    }

    // コマンドアロケータを生成
    result = dev->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&cmdAllocator));

    // コマンドリストを生成
    result = dev->CreateCommandList(0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        cmdAllocator, nullptr,
        IID_PPV_ARGS(&cmdList));

    // 標準設定でコマンドキューを生成
    D3D12_COMMAND_QUEUE_DESC cmdQueueDesc{};

    dev->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&cmdQueue));

    // 各種設定をしてスワップチェーンを生成
    DXGI_SWAP_CHAIN_DESC1 swapchainDesc{};
    swapchainDesc.Width = 1280;
    swapchainDesc.Height = 720;
    swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // 色情報の書式
    swapchainDesc.SampleDesc.Count = 1; // マルチサンプルしない
    swapchainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER; // バックバッファ用
    swapchainDesc.BufferCount = 2;  // バッファ数を２つに設定
    swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // フリップ後は破棄
    swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    dxgiFactory->CreateSwapChainForHwnd(
        cmdQueue,
        hwnd,
        &swapchainDesc,
        nullptr,
        nullptr,
        (IDXGISwapChain1 **)&swapchain);

    // 各種設定をしてディスクリプタヒープを生成
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type =
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // レンダーターゲットビュー
    heapDesc.NumDescriptors = 2;    // 裏表の２つ
    dev->CreateDescriptorHeap(&heapDesc,
        IID_PPV_ARGS(&rtvHeaps));
    // 裏表の２つ分について
    std::vector<ID3D12Resource *> backBuffers(2);

    for (int i = 0; i < 2; i++)
    {
        // スワップチェーンからバッファを取得
        result = swapchain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i]));
        // ディスクリプタヒープのハンドルを取得
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
        // 裏か表かでアドレスがずれる
        handle.ptr += i * dev->GetDescriptorHandleIncrementSize(heapDesc.Type);
        // レンダーターゲットビューの生成
        dev->CreateRenderTargetView(
            backBuffers[i],
            nullptr,
            handle);
    }

    // フェンスの生成
    ID3D12Fence *fence = nullptr;
    UINT64 fenceVal = 0;

    result = dev->CreateFence(fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

    IDirectInput8 *dinput = nullptr;
    result = DirectInput8Create(
        w.hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8, (void **)&dinput, nullptr);

    IDirectInputDevice8 *devkeyboard = nullptr;
    result = dinput->CreateDevice(GUID_SysKeyboard, &devkeyboard, NULL);

    result = devkeyboard->SetDataFormat(&c_dfDIKeyboard); // 標準形式

    result = devkeyboard->SetCooperativeLevel(
        hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);

    // DirectX初期化処理　ここまで
#pragma endregion DirectX初期化処理

#pragma region 描画初期化処理

    struct Vertex
    {
        XMFLOAT3 pos;
        XMFLOAT3 normal;
        XMFLOAT2 uv;
    };

    const float topheight = 10.0f;
    const unsigned int n = 5;
    Vertex vertices[18] =
    {
    };
    for (int i = 0; i < 4; i++)
    {
        vertices[i * 4].pos.x = 5 * sin(XM_2PI / 3 * i);
        vertices[i * 4 + 1].pos.x = 5 * sin(XM_2PI / 3 * i);
        vertices[i * 4 + 2].pos.x = 5 * sin(XM_2PI / 3 * i);
        vertices[i * 4 + 3].pos.x = 5 * sin(XM_2PI / 3 * i);

        vertices[i * 4].pos.y = 5 * cos(XM_2PI / 3 * i);
        vertices[i * 4 + 1].pos.y = 5 * cos(XM_2PI / 3 * i);
        vertices[i * 4 + 2].pos.y = 5 * cos(XM_2PI / 3 * i);
        vertices[i * 4 + 3].pos.y = 5 * cos(XM_2PI / 3 * i);

        vertices[i * 4].pos.z = 0;
        vertices[i * 4 + 1].pos.z = 0;
        vertices[i * 4 + 2].pos.z = 0;
        vertices[i * 4 + 3].pos.z = 0;

    }
    vertices[12].pos = { 0,0,0 };
    vertices[13].pos = { 0,0,0 };
    vertices[14].pos = { 0,0,0 };

    vertices[15].pos = { 0,0,-topheight };
    vertices[16].pos = { 0,0,-topheight };
    vertices[17].pos = { 0,0,-topheight };


    vertices[4].uv = { 0.0f,1.0f };
    vertices[0].uv = { 0.0f,0.0f };
    vertices[12].uv = { 1.0f,1.0f };

    vertices[8].uv = { 0.0f,1.0f };
    vertices[5].uv = { 0.0f,0.0f };
    vertices[13].uv = { 1.0f,1.0f };

    vertices[1].uv = { 0.0f,1.0f };
    vertices[9].uv = { 0.0f,0.0f };
    vertices[14].uv = { 1.0f,1.0f };

    vertices[2].uv = { 0.0f,1.0f };
    vertices[6].uv = { 0.0f,0.0f };
    vertices[15].uv = { 1.0f,1.0f };

    vertices[7].uv = { 0.0f,1.0f };
    vertices[10].uv = { 0.0f,0.0f };
    vertices[16].uv = { 1.0f,1.0f };

    vertices[11].uv = { 0.0f,1.0f };
    vertices[3].uv = { 0.0f,0.0f };
    vertices[17].uv = { 1.0f,1.0f };



    // 頂点バッファのサイズ = 頂点データ一つ分のサイズ * 頂点データの要素数
    UINT sizeVB = static_cast<UINT>(sizeof(Vertex) * _countof(vertices));

    D3D12_HEAP_PROPERTIES heapprop{};   // 頂点ヒープ設定
    heapprop.Type = D3D12_HEAP_TYPE_UPLOAD; // GPUへの転送用

    D3D12_RESOURCE_DESC resdesc{};  // リソース設定
    resdesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resdesc.Width = sizeVB; // 頂点情報が入る分のサイズ
    resdesc.Height = 1;
    resdesc.DepthOrArraySize = 1;
    resdesc.MipLevels = 1;
    resdesc.SampleDesc.Count = 1;
    resdesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // 頂点バッファの生成
    ID3D12Resource *vertBuff = nullptr;
    result = dev->CreateCommittedResource(
        &heapprop, // ヒープ設定
        D3D12_HEAP_FLAG_NONE,
        &resdesc, // リソース設定
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vertBuff));
    // 頂点バッファビューの作成
    D3D12_VERTEX_BUFFER_VIEW vbView{};

    vbView.BufferLocation = vertBuff->GetGPUVirtualAddress();
    vbView.SizeInBytes = sizeVB;
    vbView.StrideInBytes = sizeof(Vertex);
    // GPU上のバッファに対応した仮想メモリを取得
    Vertex *vertMap = nullptr;
    result = vertBuff->Map(0, nullptr, (void **)&vertMap);
    // 全頂点に対して
    for (int i = 0; i < _countof(vertices); i++)
    {
        vertMap[i] = vertices[i];   // 座標をコピー
    }
    // マップを解除
    vertBuff->Unmap(0, nullptr);
    

    //unsigned short indices[] = {
    //0,1,2,//前
    // 2,1,3,
    //  5,4,6,//後
    //  5,6,7,
    // 
    // 8,9,10,//左
    //  10,9,11,
    //
    //  12,13,14,//右
    //  14,13,15,
    //
    //  16,17,18,//下
    //  18,17,19,
    //
    //  21,20,22,//上
    //  21,22,23,
    //
    //};
    
    //↑今までの奴

    unsigned short indices[18] = 
    {
    4,0,12,
    8,5,13,
    1,9,14,
    2,6,15,
    7,10,16,
    11,3,17,
    };
    

    for (int i = 0; i < _countof(indices) / 3; i++)
    {
        unsigned short indice0 = indices[i * 3 + 0];
        unsigned short indice1 = indices[i * 3 + 1];
        unsigned short indice2 = indices[i * 3 + 2];

        XMVECTOR p0 = XMLoadFloat3(&vertices[indice0].pos);
        XMVECTOR p1 = XMLoadFloat3(&vertices[indice1].pos);
        XMVECTOR p2 = XMLoadFloat3(&vertices[indice2].pos);

        XMVECTOR v1 = XMVectorSubtract(p1, p0);
        XMVECTOR v2 = XMVectorSubtract(p2, p0);

        XMVECTOR normal = XMVector3Cross(v1, v2);

        normal = XMVector3Normalize(normal);

        XMStoreFloat3 (&vertices[indice0].normal,normal);
        XMStoreFloat3 (&vertices[indice1].normal,normal);
        XMStoreFloat3 (&vertices[indice2].normal,normal);
    }


    // インデックスバッファの設定
    ID3D12Resource *indexBuff = nullptr;
    resdesc.Width = sizeof(indices); // インデックス情報が入る分のサイズ
    // インデックスバッファの生成
    result = dev->CreateCommittedResource(
        &heapprop, // ヒープ設定
        D3D12_HEAP_FLAG_NONE,
        &resdesc, // リソース設定
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&indexBuff));

    // GPU上のバッファに対応した仮想メモリを取得
    unsigned short *indexMap = nullptr;
    result = indexBuff->Map(0, nullptr, (void **)&indexMap);

    // 全インデックスに対して
    for (int i = 0; i < _countof(indices); i++)
    {
        indexMap[i] = indices[i];   // インデックスをコピー
    }
    // 繋がりを解除
    indexBuff->Unmap(0, nullptr);

    D3D12_INDEX_BUFFER_VIEW ibView{};
    ibView.BufferLocation = indexBuff->GetGPUVirtualAddress();
    ibView.Format = DXGI_FORMAT_R16_UINT;
    ibView.SizeInBytes = sizeof(indices);


    // ヒープ設定
    D3D12_HEAP_PROPERTIES cbheapprop{};                         // ヒープ設定
    cbheapprop.Type = D3D12_HEAP_TYPE_UPLOAD;                   // GPUへの転送用
    // リソース設定
    D3D12_RESOURCE_DESC cbresdesc{};                            // リソース設定
    cbresdesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbresdesc.Width = (sizeof(ConstBufferData) + 0xff) & ~0xff;   // 256バイトアラインメント
    cbresdesc.Height = 1;
    cbresdesc.DepthOrArraySize = 1;
    cbresdesc.MipLevels = 1;
    cbresdesc.SampleDesc.Count = 1;
    cbresdesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    XMMATRIX matProjection = XMMatrixPerspectiveFovLH(
        XMConvertToRadians(60.0f),
        (float)window_width / window_height,
        0.1f, 1000.0f
    );

        XMMATRIX matView;
        XMFLOAT3 eye(0, 0, -100);
        XMFLOAT3 target(0, 0, 0);
        XMFLOAT3 up(0, 1, 0);
        matView = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMLoadFloat3(&up));



        float angle = 0.0f;


    // 定数バッファ用デスクリプタヒープの生成
    ID3D12DescriptorHeap *basicDescHeap = nullptr;
    // 設定構造体
    D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc{};
    descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // シェーダーから見える
    descHeapDesc.NumDescriptors = constantBufferNum + 1;

    // 生成
   
    result = dev->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&basicDescHeap));
    D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandleSRV = basicDescHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandleSRV = basicDescHeap->GetGPUDescriptorHandleForHeapStart();

    cpuDescHandleSRV.ptr += constantBufferNum * dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    gpuDescHandleSRV.ptr += constantBufferNum * dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


    for (int i = 0; i < _countof(object3ds); i++)
    {
        InitializeObject3d(&object3ds[i], i, dev, basicDescHeap);

        if (i > 0)
        {
             object3ds[i].parent   = &object3ds[i - 1];
            object3ds[i].scale    = { 0.9f,0.9f,0.9f };
            object3ds[i].rotation = { 0.0f,0.0f,30.0f };
            object3ds[i].position = { 0.0f,0.0f,-8.0f, };
        }
    }

    for (int i = 0; i < _countof(object3ds); i++)
    {
        UpdateObject3d(&object3ds[i], matView, matProjection);
    }

  

    ID3DBlob *vsBlob = nullptr; // 頂点シェーダオブジェクト
    ID3DBlob *psBlob = nullptr; // ピクセルシェーダオブジェクト
    ID3DBlob *errorBlob = nullptr; // エラーオブジェクト


  
    TexMetadata metadata{};
    ScratchImage scratchImg{};

    result = LoadFromWICFile(
        L"Resources/a.png",
        WIC_FLAGS_NONE,
        &metadata, scratchImg);
    const Image *img = scratchImg.GetImage(0, 0, 0);


    D3D12_HEAP_PROPERTIES texHeapProp{};
    texHeapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
    texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

    D3D12_RESOURCE_DESC texresDesc{};
    texresDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);
    texresDesc.Format = metadata.format;
    texresDesc.Width = metadata.width;
    texresDesc.Height = (UINT)metadata.height;
    texresDesc.DepthOrArraySize = (UINT16)metadata.arraySize;
    texresDesc.MipLevels = (UINT16)metadata.mipLevels;
    texresDesc.SampleDesc.Count = 1;

    ID3D12Resource *texbuff = nullptr;
    result = dev->CreateCommittedResource(
        &texHeapProp,
        D3D12_HEAP_FLAG_NONE,
        &texresDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&texbuff)
    );
    result = texbuff->WriteToSubresource(
        0,
        nullptr,
        img->pixels,
       (UINT)img->rowPitch,
       (UINT)img->slicePitch
    );
    //delete[] imageData;

   

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture1D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE basicHeapHandle2 = basicDescHeap->GetCPUDescriptorHandleForHeapStart();
    basicHeapHandle2.ptr += constantBufferNum * dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    dev->CreateShaderResourceView(texbuff,
        &srvDesc,
        basicHeapHandle2
    );

    // 頂点シェーダの読み込みとコンパイル
    result = D3DCompileFromFile(
        L"BasicVS.hlsl",  // シェーダファイル名
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, // インクルード可能にする
        "main", "vs_5_0", // エントリーポイント名、シェーダーモデル指定
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, // デバッグ用設定
        0,
        &vsBlob, &errorBlob);

    if (FAILED(result)) {
        // errorBlobからエラー内容をstring型にコピー
        std::string errstr;
        errstr.resize(errorBlob->GetBufferSize());

        std::copy_n((char *)errorBlob->GetBufferPointer(),
            errorBlob->GetBufferSize(),
            errstr.begin());
        errstr += "\n";
        // エラー内容を出力ウィンドウに表示
        OutputDebugStringA(errstr.c_str());
        exit(1);
    }

    // ピクセルシェーダの読み込みとコンパイル
    result = D3DCompileFromFile(
        L"BasicPS.hlsl",   // シェーダファイル名
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, // インクルード可能にする
        "main", "ps_5_0", // エントリーポイント名、シェーダーモデル指定
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, // デバッグ用設定
        0,
        &psBlob, &errorBlob);

    if (FAILED(result)) {
        // errorBlobからエラー内容をstring型にコピー
        std::string errstr;
        errstr.resize(errorBlob->GetBufferSize());

        std::copy_n((char *)errorBlob->GetBufferPointer(),
            errorBlob->GetBufferSize(),
            errstr.begin());
        errstr += "\n";
        // エラー内容を出力ウィンドウに表示
        OutputDebugStringA(errstr.c_str());
        exit(1);
    }

    // 頂点レイアウト
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,D3D12_APPEND_ALIGNED_ELEMENT,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // グラフィックスパイプライン設定
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gpipeline{};

    gpipeline.VS.pShaderBytecode = vsBlob->GetBufferPointer();
    gpipeline.VS.BytecodeLength = vsBlob->GetBufferSize();
    gpipeline.PS.pShaderBytecode = psBlob->GetBufferPointer();
    gpipeline.PS.BytecodeLength = psBlob->GetBufferSize();

    gpipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK; // 標準設定

    gpipeline.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;  // カリングしない
    gpipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; // ポリゴン内塗りつぶし
    gpipeline.RasterizerState.DepthClipEnable = true; // 深度クリッピングを有効に

    // レンダーターゲットのブレンド設定
    D3D12_RENDER_TARGET_BLEND_DESC &blenddesc = gpipeline.BlendState.RenderTarget[0];
    blenddesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; // 標準設定
    blenddesc.BlendEnable = true;                   // ブレンドを有効にする
    blenddesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;    // 加算
    blenddesc.SrcBlendAlpha = D3D12_BLEND_ONE;      // ソースの値を100% 使う
    blenddesc.DestBlendAlpha = D3D12_BLEND_ZERO;    // デストの値を   0% 使う
    blenddesc.BlendOp = D3D12_BLEND_OP_ADD;             // 加算
    blenddesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;         // ソースのアルファ値
    blenddesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;    // 1.0f-ソースのアルファ値

    gpipeline.InputLayout.pInputElementDescs = inputLayout;
    gpipeline.InputLayout.NumElements = _countof(inputLayout);

    gpipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    gpipeline.NumRenderTargets = 1; // 描画対象は1つ
    gpipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // 0～255指定のRGBA
    gpipeline.SampleDesc.Count = 1; // 1ピクセルにつき1回サンプリング

    gpipeline.DepthStencilState.DepthEnable = true;
    gpipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    gpipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    gpipeline.DSVFormat = DXGI_FORMAT_D32_FLOAT;



    // デスクリプタテーブルの設定
    D3D12_DESCRIPTOR_RANGE descRangeCBV{};
    descRangeCBV.NumDescriptors = 1;                            // 定数ひとつ
    descRangeCBV.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;   // 種別は定数
    descRangeCBV.BaseShaderRegister = 0;                        // 0番スロットから
    descRangeCBV.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    // デスクリプタテーブルの設定
    D3D12_DESCRIPTOR_RANGE descRangeSRV{};
    descRangeSRV.NumDescriptors = 1;                            // 定数ひとつ
    descRangeSRV.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;   // 種別は定数
    descRangeSRV.BaseShaderRegister = 0;                        // 0番スロットから
    descRangeSRV.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;  // 標準

    // ルートパラメータの設定
    D3D12_ROOT_PARAMETER rootparams[2] = {};
    rootparams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;   //種類
    rootparams[0].DescriptorTable.pDescriptorRanges = &descRangeCBV;            //デスクリプタレンジ
    rootparams[0].DescriptorTable.NumDescriptorRanges = 1;                      //デスクリプタレンジ数
    rootparams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootparams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;   //種類
    rootparams[1].DescriptorTable.pDescriptorRanges = &descRangeSRV;            //デスクリプタレンジ
    rootparams[1].DescriptorTable.NumDescriptorRanges = 1;                      //デスクリプタレンジ数
    rootparams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    //全てのシェーダから見える
    D3D12_STATIC_SAMPLER_DESC samplerDesc{};

    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;


    // ルートシグネチャの生成
    ID3D12RootSignature *rootsignature;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignatureDesc.pParameters = rootparams; //ルートパラメータの先頭アドレス
    rootSignatureDesc.NumParameters = _countof(rootparams);        //ルートパラメータ数
    rootSignatureDesc.pStaticSamplers = &samplerDesc;
    rootSignatureDesc.NumStaticSamplers = 1;

    ID3DBlob *rootSigBlob = nullptr;
    result = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rootSigBlob, &errorBlob);
    result = dev->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(&rootsignature));
    rootSigBlob->Release();

    // パイプラインにルートシグネチャをセット
    gpipeline.pRootSignature = rootsignature;

    ID3D12PipelineState *pipelinestate = nullptr;
    result = dev->CreateGraphicsPipelineState(&gpipeline, IID_PPV_ARGS(&pipelinestate));

    D3D12_RESOURCE_DESC depthResDesc{};
    depthResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthResDesc.Width = window_width;
    depthResDesc.Height = window_height;
    depthResDesc.DepthOrArraySize = 1;
    depthResDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthResDesc.SampleDesc.Count = 1;
    depthResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES depthHeapProp{};
    depthHeapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE depthClearValue{};
    depthClearValue.DepthStencil.Depth = 1.0f;
    depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;

    ID3D12Resource *depthBuffer = nullptr;
    result = dev->CreateCommittedResource(
        &depthHeapProp,
        D3D12_HEAP_FLAG_NONE,
        &depthResDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClearValue,
        IID_PPV_ARGS(&depthBuffer));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ID3D12DescriptorHeap *dsvHeap = nullptr;
    result = dev->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dev->CreateDepthStencilView(
        depthBuffer,
        &dsvDesc,
        dsvHeap->GetCPUDescriptorHandleForHeapStart()
    );
    
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle0;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle1;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle2;

    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandleStart = basicDescHeap->GetGPUDescriptorHandleForHeapStart();

    UINT descHeapHandleIncrementSize = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

 

#pragma endregion 描画初期化処理

    while (true)  // ゲームループ
    {
#pragma region ウィンドウメッセージ処理
        // メッセージがある？
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); // キー入力メッセージの処理
            DispatchMessage(&msg); // プロシージャにメッセージを送る
        }

        // ✖ボタンで終了メッセージが来たらゲームループを抜ける
        if (msg.message == WM_QUIT) {
            break;
        }
#pragma endregion ウィンドウメッセージ処理

#pragma region DirectX毎フレーム処理
        // DirectX毎フレーム処理　ここから

        result = devkeyboard->Acquire();

        BYTE key[256] = {};
        result = devkeyboard->GetDeviceState(sizeof(key), key);

        if (key[DIK_0]) // 数字の0キーが押されていたら
        {
            OutputDebugStringA("Hit 0\n");  // 出力ウィンドウに「Hit 0」と表示
        }

        float clearColor[] = { 0.1f,0.25f, 0.5f,0.0f }; // 青っぽい

        const int cycle = 60;
        counter++;
        counter %= cycle;
        //float scale = sinf(XM_2PI * (float)counter / cycle);
        //scale += 1.0f;
        //scale /= 2.0f;
        //const float min_value = 2.0f;
        //const float max_value = 5.0f;

        //scale = min_value + (max_value - min_value) * scale;

        ////float scale = (float)counter / cycle;
        //object3ds[0].scale = { scale,scale,scale };


        //①
       /* float rot = 180 * (float)counter / cycle;

        object3ds[0].rotation.y = rot;
        */

        //②
       /*  float scale = XM_2PI * (float)counter / cycle;
        scale += 1.0f;
        scale /= 2.0f;
        const float min_value = 0.1f;
        const float max_value = 2.0f;
        scale = min_value + (max_value - min_value) * scale;
        object3ds[0].scale = { scale,scale,scale };*/

        //③
        /*float pos = 150 * (float)counter / cycle;
        pos -= 75;
        object3ds[0].position.x = pos;*/

        ////④
        //float rot = sinf(180* (float)counter);
        //rot += 1.0f;
        //rot /= 2.0f;
        //object3ds[0].rotation.y = rot;


        if (key[DIK_SPACE])     // スペースキーが押されていたら
        {
            // 画面クリアカラーの数値を書き換える
            clearColor[1] = 1.0f;
         
        }


        if (key[DIK_UP] || key[DIK_DOWN] || key[DIK_LEFT] || key[DIK_RIGHT])
        {


            if (key[DIK_UP]) { object3ds[0].position.y += 1.0f; }
            else if (key[DIK_DOWN]) { object3ds[0].position.y -= 1.0f; }
            if (key[DIK_RIGHT]) { object3ds[0].position.x += 1.0f; }
            else if (key[DIK_LEFT]) { object3ds[0].position.x -= 1.0f; }


        }

        
        if (key[DIK_D] || key[DIK_A] || key[DIK_W] || key[DIK_S])
        {
            if (key[DIK_D]) { object3ds[0].rotation.y += 1.0f; }
            else if (key[DIK_A]) { object3ds[0].rotation.y -= 1.0f; }
            else if (key[DIK_W]) { object3ds[0].rotation.x += 1.0f; }
            else if (key[DIK_S]) { object3ds[0].rotation.x -= 1.0f; }

    
            
        }
        for (int i = 0; i < _countof(object3ds); i++)
        {
            UpdateObject3d(&object3ds[i], matView, matProjection);
        }
        // GPU上のバッファに対応した仮想メモリを取得
        Vertex *vertMap = nullptr;
        result = vertBuff->Map(0, nullptr, (void **)&vertMap);
        // 全頂点に対して
        for (int i = 0; i < _countof(vertices); i++)
        {
            vertMap[i] = vertices[i];   // 座標をコピー
        }
        // マップを解除
        vertBuff->Unmap(0, nullptr);

        // DirectX毎フレーム処理　ここまで
#pragma endregion DirectX毎フレーム処理

#pragma region グラフィックスコマンド
        // バックバッファの番号を取得（2つなので0番か1番）
        UINT bbIndex = swapchain->GetCurrentBackBufferIndex();

        // １．リソースバリアで書き込み可能に変更
        D3D12_RESOURCE_BARRIER barrierDesc{};
        barrierDesc.Transition.pResource = backBuffers[bbIndex]; // バックバッファを指定
        barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; // 表示から
        barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET; // 描画
        cmdList->ResourceBarrier(1, &barrierDesc);

        // ２．描画先指定
        // レンダーターゲットビュー用ディスクリプタヒープのハンドルを取得
        D3D12_CPU_DESCRIPTOR_HANDLE rtvH = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
        rtvH.ptr += bbIndex * dev->GetDescriptorHandleIncrementSize(heapDesc.Type);
       // cmdList->OMSetRenderTargets(1, &rtvH, false, nullptr);
        D3D12_CPU_DESCRIPTOR_HANDLE dsvH = dsvHeap->GetCPUDescriptorHandleForHeapStart();
        cmdList->OMSetRenderTargets(1, &rtvH, false,&dsvH);

       


        // ３．画面クリア           R     G     B    A
        //float clearColor[] = { 0.1f,0.25f, 0.5f,0.0f }; // 青っぽい色
        cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);
        cmdList->ClearDepthStencilView(dsvH, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // ４．描画コマンドここから
        // パイプラインステートとルートシグネチャの設定
        cmdList->SetPipelineState(pipelinestate);
        cmdList->SetGraphicsRootSignature(rootsignature);
        // デスクリプタヒープをセット

        // ビューポート領域の設定
        D3D12_VIEWPORT viewport{};
        viewport.Width = window_width;
        viewport.Height = window_height;
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        cmdList->RSSetViewports(1, &viewport);

        // シザー矩形の設定
        D3D12_RECT scissorrect{};
        scissorrect.left = 0;                                       // 切り抜き座標左
        scissorrect.right = scissorrect.left + window_width;        // 切り抜き座標右
        scissorrect.top = 0;                                        // 切り抜き座標上
        scissorrect.bottom = scissorrect.top + window_height;       // 切り抜き座標下
        cmdList->RSSetScissorRects(1, &scissorrect);

        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


        for (int i = 0; i < _countof(object3ds); i++)
        {
            DrawObject3d(&object3ds[i], cmdList, basicDescHeap, vbView,ibView, gpuDescHandleSRV, _countof(indices));
        }

        // ４．描画コマンドここまで

        // ５．リソースバリアを戻す
        barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; // 描画
        barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;   // 表示に
        cmdList->ResourceBarrier(1, &barrierDesc);

        // 命令のクローズ
        cmdList->Close();
        // コマンドリストの実行
        ID3D12CommandList *cmdLists[] = { cmdList }; // コマンドリストの配列
        cmdQueue->ExecuteCommandLists(1, cmdLists);
        // コマンドリストの実行完了を待つ
        cmdQueue->Signal(fence, ++fenceVal);
        if (fence->GetCompletedValue() != fenceVal) {
            HANDLE event = CreateEvent(nullptr, false, false, nullptr);
            fence->SetEventOnCompletion(fenceVal, event);
            WaitForSingleObject(event, INFINITE);
            CloseHandle(event);
        }

        cmdAllocator->Reset(); // キューをクリア
        cmdList->Reset(cmdAllocator, nullptr);  // 再びコマンドリストを貯める準備
#pragma endregion グラフィックスコマンド

        // バッファをフリップ（裏表の入替え）
        swapchain->Present(1, 0);

    }
#pragma region WindowsAPI後始末
    // ウィンドウクラスを登録解除
    UnregisterClass(w.lpszClassName, w.hInstance);
#pragma endregion WindowsAPI後始末

    return 0;
}