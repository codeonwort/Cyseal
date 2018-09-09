# Coding Convention

## Enums start with `E`
```
enum class ERenderDeviceRawAPI
{
	DirectX12,
	Vulkan
};
```

## Curly brackets start at a new line.
```
struct SomeStruct
{
	TypeA valueA;
	TypeB valueB;
};

if(expr)
{
	doSomething();
}
```

## Classes, structs, enums start with an upper letter.
```
class SomeClass
{
};
struct SomeStruct
{
};
enum class ESomeEnum
{
};
```

## Variables, functions start with a lower letter.
```
uint8_t     someVar;
SomeClass*  someInstance;
void        doSomething();
```

## Macros are all upper cases.
* CHECK()
* SCOPED_CYCLE_COUNTER()
* HR()

## File names are all lower cases and follow underscore style.
* d3d_device.h
* vk_device.h
* unit_test.h

