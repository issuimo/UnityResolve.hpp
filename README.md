> [!NOTE]\
> 有新的功能建议或者Bug可以提交Issues (当然你也可以尝试自己修改后提交\
> New feature suggestions or bugs can be commit as issues. Of course, you can also try modifying it yourself and then commit.

> Dome
> - [Phasmophobia Cheat](https://github.com/issuimo/PhasmophobiaCheat/tree/main)

> 如果在调用Unity函数时发生崩溃情况可以使用以下方法解决 (仅il2cpp)\
> If a crash occurs when calling Unity functions, you can use the following methods to resolve it. (il2cpp only)
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
> - [x] MonoBehaviour
> - [x] Renderer
> - [x] Mesh
> - [X] Behaviour
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
> - [X] Type (C#)
> - [X] List
> - [X] Dictionary
> - More...

> ### 功能 (Function)
> - [X] DumpToFile
> - [X] 附加线程 (Thread Attach / Detach)
> - [X] 修改静态变量值 (Modifying the value of a static variable)
> - [X] 获取实例 (Obtaining an instance)
> - [X] 创建C#字符串 (Create C# String)
> - [X] 创建C#数组 (Create C# Array)
> - [X] 世界坐标转屏幕坐标/屏幕坐标转世界坐标 (WorldToScreenPoint/ScreenToWorldPoint)
> - [X] 获取继承子类的名称 (Get the name of the inherited subclass)
> - [X] 获取函数地址(变量偏移) 及调用(修改/获取) (Get the function address (variable offset) and invoke (modify/get))
> - [x] 获取Gameobject组件 (Get GameObject component)
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

#### 获取函数地址(变量偏移) 及调用(修改/获取) (Get the function address (variable offset) and invoke (modify/get))
``` c++
const auto assembly = UnityResolve::Get("assembly.dll | 程序集名称.dll");
const auto pClass   = assembly->Get("className | 类名称");
                   // assembly->Get("className | 类名称", "*");
                   // assembly->Get("className | 类名称", "namespace | 空间命名");

const auto field       = pClass->Get<UnityResolve::Field>("Field | 变量名");
const auto fieldOffset = pClass->Get<std::int32_t>("Field | 变量名");
const int  time        = pClass->GetValue<int>(obj Instance | 对象地址, "time");
                      // pClass->GetValue(obj Instance*, name);
                       = pClass->SetValue<int>(obj Instance | 对象地址, "time", 114514);
                      // pClass->SetValue(obj Instance*, name, value);
const auto method      = pClass->Get<UnityResolve::Method>("Method | 函数名");
                      // pClass->Get<UnityResolve::Method>("Method | 函数名", { "System.String" });
                      // pClass->Get<UnityResolve::Method>("Method | 函数名", { "*", "System.String" });
                      // pClass->Get<UnityResolve::Method>("Method | 函数名", { "*", "", "System.String" });
                      // pClass->Get<UnityResolve::Method>("Method | 函数名", { "*", "System.Int32", "System.String" });
                      // pClass->Get<UnityResolve::Method>("Method | 函数名", { "*", "System.Int32", "System.String", "*" });
                      // "*" == ""

const auto functionPtr = method->function;

const auto method1 = pClass->Get<Method>("method name1 | 函数名称1");
const auto method2 = pClass->Get<Method>("method name2 | 函数名称2");

method1->Invoke<int>(114, 514, "114514");
// Invoke<return type>(args...);

const auto ptr = method2->Cast<void, int, bool>();
// Cast<return type, args...>(void);
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
// FindObjectsByType<return type>(void);
playerVector.size();
```
#### 世界坐标转屏幕坐标/屏幕坐标转世界坐标 (WorldToScreenPoint/ScreenToWorldPoint)
``` c++
Camera* pCamera = UnityResolve::UnityType::Camera::GetMain();
Vector3 point   = pCamera->WorldToScreenPoint(Vector3, Eye::Left);
Vector3 world   = pCamera->ScreenToWorldPoint(point, Eye::Left);
```
#### 获取继承子类的名称 (Get the name of the inherited subclass)
> [!NOTE]\
> 在找不到某一个实体类的情况下这很有用 \
> This is very useful in cases where a certain entity class cannot be found.
``` c++
const auto assembly = UnityResolve::Get("UnityEngine.CoreModule.dll");
const auto pClass   = assembly->Get("MonoBehaviour");
Parent* pParent     = pClass->FindObjectsByType<Parent*>()[0];
std::string child   = pParent->GetType()->FormatTypeName();
```
#### 获取Gameobject组件 (Get GameObject component)
``` c++
std::vector<T*> objs = gameobj->GetComponents<T*>(UnityResolve::Get("assembly.dll")->Get("class")));
                    // gameobj->GetComponents<return type>(Class* component)
std::vector<T*> objs = gameobj->GetComponentsInChildren<T*>(UnityResolve::Get("assembly.dll")->Get("class")));
                    // gameobj->GetComponentsInChildren<return type>(Class* component)
std::vector<T*> objs = gameobj->GetComponentsInParent<T*>(UnityResolve::Get("assembly.dll")->Get("class")));
                    // gameobj->GetComponentsInParent<return type>(Class* component)
```
