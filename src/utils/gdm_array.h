#ifndef GDM_UTILS_ARRAY_H
#define GDM_UTILS_ARRAY_H

#include <stdlib.h>
#include <string.h>

// 动态数组
typedef struct array
{
    // 数据缓冲区
    void* data;
    // 单个数组元素的字节数大小
    size_t element_size;
    // 有效数组元素的个数
    size_t element_num;
    // 能存放元素的个数
    size_t element_capacity;
} t_array;

static void array_free(t_array* self);

// 构造动态数组，element_size为单个元素的字节长度，capacity为初始容量
static t_array* array_new(size_t element_size, size_t capacity)
{
    const size_t data_length = capacity * element_size;
    t_array*     ret         = (t_array*)malloc(sizeof(t_array));
    if (ret == NULL)
    {
        return NULL;
    }

    ret->element_size     = element_size;
    ret->element_num      = 0;
    ret->data             = malloc(data_length);
    ret->element_capacity = capacity;

    if (ret->data == NULL)
    {
        array_free(ret);
        ret = NULL;
    }

    return ret;
}

static void array_free(t_array* self)
{
    if (self == NULL)
    {
        return;
    }

    if (self->data != NULL)
    {
        free(self->data);
        self->data = NULL;
    }

    free(self);
    self = NULL;
}

// 返回数组指定索引的成员的地址
static void* array_at(const t_array* self, size_t index)
{
    char* ret = NULL;

    if (index < self->element_num)
    {
        ret = (char*)(self->data) + (index * self->element_size);
    }

    return (void*)ret;
}

static void array_push_back(t_array* self, const void* data)
{
    if (self->element_num == self->element_capacity)
    {
        const size_t new_element_capacity = self->element_capacity * 2 + 1;
        const size_t new_data_length      = new_element_capacity * self->element_size;
        self->data                        = realloc(self->data, new_data_length);
        self->element_capacity            = new_element_capacity;
    }

    memcpy((void*)((const char*)self->data + self->element_num * self->element_size), data, self->element_size);
    self->element_num++;
}

static inline void array_reset(t_array* self)
{
    self->element_num = 0;
}

// 返回数组的有效元素个数
static size_t array_num(const t_array* self)
{
    return self->element_num;
}

#endif
