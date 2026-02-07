#include <windows.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct      //структура: указатель на обобщённую функцию и параметр для неё
{
    void (*func)(void *);    //сигнатура такой функции: void func(void *someArg);
    void *arg;
} GeneralizedUnit;

struct tag_cleanupStack      //позволяет освобождать все занятые ресурсы по принцыпу стека (LIFO)
{
    GeneralizedUnit *deallocatingUnits;    //храним массив всех ресурсов системы – ресурс и инструкция к очистке
    uint8_t size;        //количество зарегистрированных ресурсов системы ∈ [0; 255]
};
typedef struct tag_cleanupStack* CleanupStack;  //Передаём этот объект в функции всегда как указатель, никогда напрямую с ним внутри main не работаем

CleanupStack InitCleanupStack(uint8_t totalResourcesQuantity)   /*создаёт объект "стека очистки" с необходимым размером. 
Как параметр принемает максимальное количество существующих одновременно ресурсов системы на 1 "стек очистки"          */
{
    if(totalResourcesQuantity == 0 || totalResourcesQuantity == 255) return NULL;    //валидация: ресурсов для стека очистки может быть ∈ [1; 254]
    CleanupStack tempPtr = malloc(sizeof(*tempPtr));
    if (tempPtr)    //если malloc вернул не NULL
    {
        tempPtr->deallocatingUnits = malloc(sizeof(GeneralizedUnit) * totalResourcesQuantity); //получаем место под массив для регистрации ресурсов системы
        if (!tempPtr->deallocatingUnits)    //если malloc вернул NULL
        {
            free(tempPtr);
            return NULL;
        }
        tempPtr->size = 0;  //изначально ресурсов системы не выделено => стек пустой
    }
    return tempPtr; //возвращает либо указатель на созданый объект, либо NULL
}

void PushCleanupStack(CleanupStack cs, void (*func)(void *), void *arg)  //регистрируем ресурс, передавая функцию, которая вернёт его системе и указатель на сам ресурс
{
    cs->deallocatingUnits[cs->size++] = (GeneralizedUnit){ func, arg }; //записываем полученные указатели в стек и увеличиваем количество зарегистрированных ресурсов на 1
}

void SingleDeallocation(CleanupStack cs)
{
    --cs->size;
    cs->deallocatingUnits[cs->size].func(cs->deallocatingUnits[cs->size].arg);  //выполняем заранее зарегистрированную инструкцию к возвращению ресурса системе
}

void PartialDeallocation(CleanupStack cs, uint8_t iterations) //очистить частично
{
    while (iterations > 0)
    {
        --iterations;
        SingleDeallocation(cs);
    }
}

void CompleteDeallocation(CleanupStack cs)   //полностью очищаем систему перед завершением программы
{
    PartialDeallocation(cs, cs->size);
    free(cs->deallocatingUnits);
    free(cs);
    cs = NULL;
}
