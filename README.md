> [!NOTE]\
> 有新的功能建议或者Bug可以提交Issues \
> New feature suggestions or bugs can be submitted as issues.

> 如果在调用Unity函数时发生崩溃情况可以使用以下方法解决\
> If a crash occurs when calling Unity functions, you can use the following methods to resolve it.
> ``` c++
> __try {
>   // your code
> 
>   // if error C2712
>   [&]() {
>     // your code
>   }();
> } __except (EXCEPTION_EXECUTE_HANDLER) {
>    return;
> }
> ```

# UnityResolve.hpp
> ### 类型 (Type)
> - [X] Camera
> - [X] Transform
> - [X] Component
> - [X] Object (Unity)
> - [X] LayerMask
> - [X] Rigidbody
> - [X] Physics
> - [X] GameObject
> - [X] Collider
> - [X] Vector4
> - [X] Vector3
> - [X] Vector2
> - [X] Quaternion
> - [X] Bounds
> - [X] Plane
> - [X] Ray
> - [X] Rect
> - [X] Color
> - [X] Matrix4x4
> - [X] Array
> - [x] String
> - [x] Object (C#)
> - [X] List
> - [X] Dictionary
> - More...

> ### 功能 (Function)
> - [X] DumpToFile
> - [X] 修改静态变量值 (Modifying the value of a static variable)
> - [X] 获取实例 (Obtaining an instance)
> - More...

#### 初始化 (Initialization)
``` c++
UnityResolve::Init(GetModuleHandle(L"GameAssembly.dll | mono.dll"), UnityResolve::Mode::Auto);
```
> 参数1: dll句柄 \
> Parameter 1: DLL handle \
> 参数2: 使用模式 \
> Parameter 2: Usage mode
> - Mode::Il2cpp
> - Mode::Mono
> - Mode::Auto

#### 附加线程 (Thread Attach / Detach)
``` c++
// C# GC Attach
UnityResolve::ThreadAttach();

// C# GC Detach
UnityResolve::ThreadDetach();
```

#### 获取函数地址及调用 (Obtaining Function Addresses and Invoking)
``` c++
const auto assembly = UnityResolve::Get("assembly.dll | 程序集名称.dll");
const auto pClass   = assembly->Get("className | 类名称");
                   // assembly->Get("className | 类名称", "namespace | 空间命名");

const auto field  = pClass->Get<UnityResolve::Field>("Field | 变量名");
const auto method = pClass->Get<UnityResolve::Method>("Method | 函数名");
                 // pClass->Get<UnityResolve::Method>("Method | 函数名", { "System.String" });
                 // pClass->Get<UnityResolve::Method>("Method | 函数名", { "*", "System.String" });
                 // pClass->Get<UnityResolve::Method>("Method | 函数名", { "*", "", "System.String" });
                 // pClass->Get<UnityResolve::Method>("Method | 函数名", { "*", "", "System.String", "*" });
                 // "*" == ""

const auto functionPtr = method->function;

const auto method1 = pClass->Get<Method>("method name1 | 函数名称1");
const auto method2 = pClass->Get<Method>("method name2 | 函数名称2");

method1->Invoke<int>(114, 514, "114514");

const auto ptr = method2->Cast<void, int, bool>();
ptr(114514, true);
```
#### 转存储到文件 (DumpToFile)
``` C++
UnityResolve::DumpToFile("./Dump.cs");
```
#### 创建C#字符串 (Create C# String)
``` c++
const auto str     = UnityResolve::UnityType::String::New("string | 字符串");
std::string cppStr = str.ToString();
```
#### 创建C#数组 (Create C# Array)
``` c++
const auto assembly = UnityResolve::Get("assembly.dll | 程序集名称.dll");
const auto pClass   = assembly->Get("className | 类名称");
const auto array    = UnityResolve::UnityType::Array::New(pClass, size);
std::vector<T> cppVector = array.ToVector();
```
#### 获取实例 (Obtaining an instance)
``` c++
const auto assembly = UnityResolve::Get("assembly.dll | 程序集名称.dll");
const auto pClass   = assembly->Get("className | 类名称");
std::vector<Player*> playerVector = pClass->FindObjectsByType<Player*>();
playerVector.size();
```
#### 世界坐标转屏幕坐标 (WorldToScreenPoint)
``` c++
Camera* pCamera = UnityResolve::UnityType::Camera::GetMain();
Vector3 point   = pCamera->WorldToScreenPoint(Vector3, Eye::Left);
```
