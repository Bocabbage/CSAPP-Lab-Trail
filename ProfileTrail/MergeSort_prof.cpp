/*
    项目：使用profiler(代码剖析工具):GPROF
          分析归并排序的条件传送写法优化情况
    数据产生：testData.cpp
    更新时间：2019\1\31(已验证)
    
*/
#include<iostream>
#include<fstream>
#define LEN 50000
#define nums 50
using std::ifstream;
using std::cout;
using std::endl;
int aux[LEN];
void merge1(int a[],int lo,int mid,int hi);
void merge2(int a[],int lo,int mid,int hi);
void MergeSort(int a[],int lo,int hi);

void merge1(int a[],int lo,int mid,int hi)
{
    /*普通的Merge子方法*/
    // 采用普通的分支处理
    int i = lo;
    int j = mid+1;
    int k;

    for(k=lo;k<hi+1;++k)
        aux[k] = a[k];
    k = lo;
    while(i<mid+1 && j<hi+1)
    {
        if(aux[i]<aux[j])
            a[k++] = aux[i++];
        else
            a[k++] = aux[j++];
    }
    while(j<hi+1 && i==mid+1)
        a[k++] = aux[j++];
    while(i<mid+1 && j==hi+1)
        a[k++] = aux[i++];

}

void merge2(int a[],int lo,int mid,int hi)
{
    /*优化为条件传送型的Merge方法*/
    int i = lo;
    int j = mid+1;
    int k;
    int v1,v2;
    bool take1;

    for(k=lo;k<hi+1;++k)
        aux[k] = a[k];
    k = lo;
    while(i<mid+1 && j<hi+1)
    {
        v1 = aux[i];
        v2 = aux[j];
        take1 = v1<v2 ? 1 : 0;
        if(take1)
            a[k++] = v1;
        else 
            a[k++] = v2;
        i += take1;
        j += (1-take1);
    }
    while(j<hi+1 && i==mid+1)
        a[k++] = aux[j++];
    while(i<mid+1 && j==hi+1)
        a[k++] = aux[i++];
}

void MergeSort(int a[],int lo,int hi)
{
    if(lo>=hi)return;
    int mid = (hi+lo)/2;
    MergeSort(a,lo,mid);
    MergeSort(a,mid+1,hi);
    merge1(a,lo,mid,hi);
    //merge2(a,lo,mid,hi);
}


int main()
{
    ifstream ifile("test.txt"); 
    int a[LEN];
    for(int k=0;k<nums;++k)
    {
        for(int j=0;j<LEN;++j)
            ifile >> a[j];
        MergeSort(a,0,LEN-1);
        for(int i=0;i<LEN;++i)
            cout<<a[i]<<endl;
        cout<<endl;
    }
    ifile.close();
    
}