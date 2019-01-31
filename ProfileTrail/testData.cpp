// 产生测试用数据的脚本
// 更新时间：2019\1\31
#include<iostream>
#include<fstream>
#include<time.h>
#define LEN 50000
#define nums 50
#define RAND 5000
using std::ofstream;

int main()
{
    srand((unsigned)time(NULL));
    ofstream ofile("test.txt");
    int i,j;
    for(i=0;i<nums;++i)
        for(j=0;j<LEN;++j)
            ofile << rand() % RAND << ' ';
    ofile.close();

}