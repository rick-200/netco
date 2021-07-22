#pragma once
#include <vector>
#include <type_traits>
template <typename T>
class delayed_list {
  std::vector<T*> v;

 public:
  void push(T* p) { v.push_back(p); }
  //����������������true��ɾ����Ԫ��
  template <typename TFunc>
  void for_each(TFunc&& f) {
    const size_t size = v.size();
    for (size_t i = 0; i < size; i++) {
      bool flag = f(v[i]);
      if (flag) v[i] = nullptr;
    }
    size_t p1 = 0, p2 = 0;
    while (true) {
      // p1�ҵ���һ��null
      while (p1 < size && v[p1] != nullptr)
        ++p1;
      if (p1 == size) break;
      p2 = std::max(p1, p2);
      //p2�ҵ�p1��ĵ�һ����null
      while (p2 < size && v[p2] == nullptr)
        ++p2;
      if (p2 == size) break;
      v[p1] = v[p2];
      v[p2] = nullptr;
      ++p1;
    }
    v.resize(p1);
  }
  size_t size() { return v.size(); }
};