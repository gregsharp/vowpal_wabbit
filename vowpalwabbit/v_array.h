/*
Copyright (c) 2009 Yahoo! Inc.  All rights reserved.  The copyrights
embodied in the content of this file are licensed under the BSD
(revised) open source license
 */

#ifndef VARRAY_H__
#define VARRAY_H__
#include <stdlib.h>
#include <string.h>

template<class T> class v_array{
 public:
  T* begin;
  T* end;
  T* end_array;

  T last() { return *(end-1);}
  T pop() { return *(--end);}
  bool empty() { return begin == end;}
  void decr() { end--;}
  v_array() { begin= NULL; end = NULL; end_array=NULL;}
  T& operator[](unsigned int i) { return begin[i]; }
  unsigned int index(){return end-begin;}
  void erase() { end = begin;}
};

template<class T> inline void push(v_array<T>& v, const T &new_ele)
{
  if(v.end == v.end_array)
    {
      size_t old_length = v.end_array - v.begin;
      size_t new_length = 2 * old_length + 3;
      //      size_t new_length = old_length + 1;
      v.begin = (T *)realloc(v.begin,sizeof(T) * new_length);
      v.end = v.begin + old_length;
      v.end_array = v.begin + new_length;
    }
  *(v.end++) = new_ele;
}

inline size_t max(size_t a, size_t b)
{ if ( a < b) return b; else return a;
}
inline size_t min(size_t a, size_t b)
{ if ( a < b) return a; else return b;
}

template<class T> void copy_array(v_array<T>& dst, v_array<T> src)
{
  dst.erase();
  push_many(dst, src.begin, src.index());
}

template<class T> void copy_array(v_array<T>& dst, v_array<T> src, T(*copy_item)(T))
{
  dst.erase();
  for (T*item = src.begin; item != src.end; item++)
    push(dst, copy_item(*item));
}

template<class T> void push_many(v_array<T>& v, const T* begin, size_t num)
{
  if(v.end+num >= v.end_array)
    {
      size_t length = v.end - v.begin;
      size_t new_length = max(2 * (size_t)(v.end_array - v.begin) + 3, 
			      v.end - v.begin + num);
      v.begin = (T *)realloc(v.begin,sizeof(T) * new_length);
      v.end = v.begin + length;
      v.end_array = v.begin + new_length;
    }
  memcpy(v.end, begin, num * sizeof(T));
  v.end += num;
}

template<class T> void reserve(v_array<T>& v, size_t length)
{
  size_t old_length = v.end_array-v.begin;
  v.begin = (T *)realloc(v.begin, sizeof(T) * length);
  if (old_length < length)
    memset(v.begin+old_length, 0, (length-old_length)*sizeof(T));
  v.end = v.begin;
  v.end_array = v.begin + length;
}

template<class T> void calloc_reserve(v_array<T>& v, size_t length)
{
  v.begin = (T *)calloc(length, sizeof(T));
  v.end = v.begin;
  v.end_array = v.begin + length;
}

template<class T> v_array<T> pop(v_array<v_array<T> > &stack)
{
  if (stack.end != stack.begin)
    return *(--stack.end);
  else
    return v_array<T>();
}

#endif  // VARRAY_H__
