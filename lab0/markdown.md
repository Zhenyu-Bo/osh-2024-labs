# Markdown

## 图片引用

![res](/src/result.png)

## 代码

```c++
#include <iostream>
#include "bubblesort.hpp"

int main() {
    std::vector<int> arr = {64, 34, 25, 12, 22, 11, 90};
    bubbleSort(arr);
    for (int num : arr) {
        std::cout << num << " ";
    }
    std::cout << std::endl;
    return 0;
}
```

## 数学公式

$f(x) = f(x_0) + \frac{f^{\prime}(x)}{1!} + \frac{f^{\prime \prime}(x)}{2!} + \dots + \frac{f^{(n)}(x)}{n!} + R_{n}$  ($R_{n} = o[(x-x_0)^n]$)
