#include <iostream>
#include <vector>

void bubbleSort(std::vector<int>& arr) {
    int n = arr.size();
    for (int i = 0; i < n - 1; ++i) {
        for (int j = 0; j < n - i - 1; ++j) {
            if (arr[j] > arr[j + 1]) {
                std::swap(arr[j], arr[j + 1]);
                #ifdef PRINT // 根据是否定义了PRINT宏来判断是否打印
                for(auto x: arr) {
                    std::cout << x <<  " ";
                }
                std::cout << std::endl;
                #endif
            }
        }
    }
}