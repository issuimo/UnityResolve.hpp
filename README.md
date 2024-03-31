> [!WARNING]\
> 该库正在重构中，届时将不在依赖一堆il2cpp/Mono导出函数, 转向直接依赖C# \
>  如获取Assembly中的class需要调用
> ``` txt
> il2cpp_domain_get_assemblies
> il2cpp_assembly_get_image
> il2cpp_image_get_class_count
> il2cpp_image_get_class
> ```
> 其处理过程非常繁琐 \
> 而现在只需要获取几个C#中几个基础类型 Assembly, Type, MethodInfo, FieldInfo \
> 按以下C#代码实现
> ``` C#
> foreach (Type type in Assembly.GetExecutingAssembly().GetTypes()) {
>        Console.WriteLine(type.ToString());
>        foreach (MethodInfo methodInfo in type.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static)) {
>            Console.WriteLine("--Method: " + methodInfo.ToString());
>        }
>        foreach (FieldInfo fieldInfo in type.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static)) {
>            Console.WriteLine("--Field: " + fieldInfo.ToString());
>        }
>  }
> ```
> 输出以下内容
> ``` txt
> Test.Program
> --Method: Void Main(System.String[])
> --Method: System.String get_Name()
> --Method: Void set_Name(System.String)
> --Method: Int32 Sub(Int32, Int32)
> --Method: Void GetType[T,T2]()
> --Method: System.Type GetType()
> --Method: System.Object MemberwiseClone()
> --Method: Void Finalize()
> --Method: System.String ToString()
> --Method: Boolean Equals(System.Object)
> --Method: Int32 GetHashCode()
> --Field: System.String name
> --Field: System.String arg
> --Field: System.String name2
> ```
> 让使用体验更加接近于C# \
> 改动不会影响类似于以前Get(*)->Get(*)->Invoke(*)这样便捷操作 \
> 此次改动将更好的适配 泛型, 继承, ..

> [!NOTE]\
> 有新的功能建议或者Bug可以提交Issues (当然你也可以尝试自己修改代码后提交到该仓库\
> New feature suggestions or bugs can be commit as issues. Of course, you can also try modifying the code yourself and then commit it to the repository.
> > Dome
> > - [Phasmophobia Cheat](https://github.com/issuimo/PhasmophobiaCheat/tree/main)
<hr>
<h3 align="center">简要概述 (Brief overview)</h3>
<hr>

# UnityResolve.hpp
> ### 支持的平台 (Supported platforms)
> - [X] Windows
> - [X] Android
> - [X] Linux
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
> - [X] Animator
> - [X] CapsuleCollider
> - [X] BoxCollider
> - More...
> ### 功能 (Function)
> - [X] DumpToFile
> - [X] 附加线程 (Thread Attach / Detach)
> - [X] 修改静态变量值 (Modifying the value of a static variable)
> - [X] 获取对象 (Obtaining an instance)
> - [X] 创建C#字符串 (Create C# String)
> - [X] 创建C#数组 (Create C# Array)
> - [X] 创建C#对象 (Create C# instance)
> - [X] 世界坐标转屏幕坐标/屏幕坐标转世界坐标 (WorldToScreenPoint/ScreenToWorldPoint)
> - [X] 获取继承子类的名称 (Get the name of the inherited subclass)
> - [X] 获取函数地址(变量偏移) 及调用(修改/获取) (Get the function address (variable offset) and invoke (modify/get))
> - [x] 获取Gameobject组件 (Get GameObject component)
> - More...
<hr>
<h3 align="center">功能使用 (How to use)</h3>
<hr>

#### 更改平台 (Change platform)
> ``` c++
> #define WINDOWS_MODE 1 // 如果需要请改为 1 | 1 if you need
> #define ANDROID_MODE 0
> #define LINUX_MODE 0
> ```

#### 初始化 (Initialization)
> ``` c++
> UnityResolve::Init(GetModuleHandle(L"GameAssembly.dll | mono.dll"), UnityResolve::Mode::Mono);
> // Linux or Android
> UnityResolve::Init(dlopen(L"GameAssembly.so | mono.so", RTLD_NOW), UnityResolve::Mode::Mono);
> ```
> 参数1: dll句柄 \
> Parameter 1: DLL handle \
> 参数2: 使用模式 \
> Parameter 2: Usage mode
> - Mode::Il2cpp
> - Mode::Mono

#### 附加线程 (Thread Attach / Detach)
> ``` c++
> // C# GC Attach
> UnityResolve::ThreadAttach();
> 
> // C# GC Detach
> UnityResolve::ThreadDetach();
> ```

#### 获取函数地址(变量偏移) 及调用(修改/获取) (Get the function address (variable offset) and invoke (modify/get))
> ``` c++
> const auto assembly = UnityResolve::Get("assembly.dll | 程序集名称.dll");
> const auto pClass   = assembly->Get("className | 类名称");
>                    // assembly->Get("className | 类名称", "*");
>                    // assembly->Get("className | 类名称", "namespace | 空间命名");
> 
> const auto field       = pClass->Get<UnityResolve::Field>("Field Name | 变量名");
> const auto fieldOffset = pClass->Get<std::int32_t>("Field Name | 变量名");
> const int  time        = pClass->GetValue<int>(obj Instance | 对象地址, "time");
>                       // pClass->GetValue(obj Instance*, name);
>                        = pClass->SetValue<int>(obj Instance | 对象地址, "time", 114514);
>                       // pClass->SetValue(obj Instance*, name, value);
> const auto method      = pClass->Get<UnityResolve::Method>("Method Name | 函数名");
>                       // pClass->Get<UnityResolve::Method>("Method Name | 函数名", { "System.String" });
>                       // pClass->Get<UnityResolve::Method>("Method Name | 函数名", { "*", "System.String" });
>                       // pClass->Get<UnityResolve::Method>("Method Name | 函数名", { "*", "", "System.String" });
>                       // pClass->Get<UnityResolve::Method>("Method Name | 函数名", { "*", "System.Int32", "System.String" });
>                       // pClass->Get<UnityResolve::Method>("Method Name | 函数名", { "*", "System.Int32", "System.String", "*" });
>                       // "*" == ""
> 
> const auto functionPtr = method->function;
> 
> const auto method1 = pClass->Get<UnityResolve::Method>("method name1 | 函数名称1");
> const auto method2 = pClass->Get<UnityResolve::Method>("method name2 | 函数名称2");
> 
> method1->Invoke<int>(114, 514, "114514");
> // Invoke<return type>(args...);
> 
> const auto ptr = method2->Cast<void, int, bool>();
> // Cast<return type, args...>(void);
> ptr(114514, true);
> ```

#### 转存储到文件 (DumpToFile)
> ``` C++
> UnityResolve::DumpToFile("./output/");
> ```

#### 创建C#字符串 (Create C# String)
> ``` c++
> const auto str     = UnityResolve::UnityType::String::New("string | 字符串");
> std::string cppStr = str.ToString();
> ```

#### 创建C#数组 (Create C# Array)
> ``` c++
> const auto assembly = UnityResolve::Get("assembly.dll | 程序集名称.dll");
> const auto pClass   = assembly->Get("className | 类名称");
> const auto array    = UnityResolve::UnityType::Array<T>::New(pClass, size);
> std::vector<T> cppVector = array.ToVector();
> ```

#### 创建C#对象 (Create C# instance)
> ``` c++
> const auto assembly = UnityResolve::Get("assembly.dll | 程序集名称.dll");
> const auto pClass   = assembly->Get("className | 类名称");
> const auto pGame    = pClass->New<Game*>();
> ```

#### 获取对象 (Obtaining an instance)
> ``` c++
> const auto assembly = UnityResolve::Get("assembly.dll | 程序集名称.dll");
> const auto pClass   = assembly->Get("className | 类名称");
> std::vector<Player*> playerVector = pClass->FindObjectsByType<Player*>();
> // FindObjectsByType<return type>(void);
> playerVector.size();
> ```

#### 世界坐标转屏幕坐标/屏幕坐标转世界坐标 (WorldToScreenPoint/ScreenToWorldPoint)
> ``` c++
> Camera* pCamera = UnityResolve::UnityType::Camera::GetMain();
> Vector3 point   = pCamera->WorldToScreenPoint(Vector3, Eye::Left);
> Vector3 world   = pCamera->ScreenToWorldPoint(point, Eye::Left);
> ```

#### 获取继承子类的名称 (Get the name of the inherited subclass)
> ``` c++
> const auto assembly = UnityResolve::Get("UnityEngine.CoreModule.dll");
> const auto pClass   = assembly->Get("MonoBehaviour");
> Parent* pParent     = pClass->FindObjectsByType<Parent*>()[0];
> std::string child   = pParent->GetType()->GetFullName();
> ```

#### 获取Gameobject组件 (Get GameObject component)
> ``` c++
> std::vector<T*> objs = gameobj->GetComponents<T*>(UnityResolve::Get("assembly.dll")->Get("class")));
>                     // gameobj->GetComponents<return type>(Class* component)
> std::vector<T*> objs = gameobj->GetComponentsInChildren<T*>(UnityResolve::Get("assembly.dll")->Get("class")));
>                     // gameobj->GetComponentsInChildren<return type>(Class* component)
> std::vector<T*> objs = gameobj->GetComponentsInParent<T*>(UnityResolve::Get("assembly.dll")->Get("class")));
>                     // gameobj->GetComponentsInParent<return type>(Class* component)
> ```

