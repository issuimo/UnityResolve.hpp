> [!NOTE]\
> 有新的功能建议或者Bug可以提交Issues \
> New feature suggestions or bugs can be submitted as issues.

# UnityResolve.hpp
> ### 类型 (Type)
> - [X] Camera
> - [X] Transform
> - [X] Component
> - [X] Object (Unity)
> - [ ] LayerMask
> - [ ] Rigidbody
> - [ ] Physics
> - [ ] Animation
> - [ ] BoxCollider
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

#### 附加线程 (Thread Attach)
``` c++
// C# GC Attach
UnityResolve::ThreadAttach();
```

#### 获取函数地址及调用 (Obtaining Function Addresses and Invoking)
``` c++
const auto classes = UnityResolve::assembly["assembly name.dll | 程序集名称.dll"]->classes;

auto klass = classes["class name | 类名称"];

const auto field = klass->Get<UnityResolve::Field>("Field | 变量名");
const auto method = klass->Get<UnityResolve::Method>("Method | 函数名");

const auto functionPtr = method->function;

const auto method1 = klass->methods["method name1 | 函数名称1"];
const auto method2 = klass->methods["method name2 | 函数名称2"];

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
auto str = UnityResolve::UnityType::String::New("string | 字符串");
std::string cppStr = str.ToString();
```
#### 创建C#数组 (Create C# Array)
``` c++
const auto classes = UnityResolve::assembly["assembly name.dll | 程序集名称.dll"]->classes;
auto klass = classes["class name | 类名称"];
auto array = UnityResolve::UnityType::Array::New(klass, size);
std::vector<T> cppVector = array.ToVector();
```
#### 获取实例 (Obtaining an instance)
``` c++
const auto ass = UnityResolve::assembly["Assembly-CSharp.dll"];
const auto klass = ass->classes["Player"];
std::vector<Player*> playerVector = klass->FindObjectsByType<Player*>();
playerVector.size();
```
#### 世界坐标转屏幕坐标 (WorldToScreenPoint)
``` c++
Camera* pCamera = UnityResolve::UnityType::Camera::GetMain();
Vector3 point = pCamera->WorldToScreenPoint(Vector3, Eye::Left);
```
