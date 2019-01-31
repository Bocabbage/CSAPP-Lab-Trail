path=/home/bocabbage/programming/ProfileTrail

# 编译以merge1为子方法的排序
# 可更改优化等级多次实验
g++ -pg -O1 $path/MergeSort_prof.cpp -o merge1
# 运行(将产生gmon.out文件)
merge1
# Profiling
gprof -b merge1 $path/gmon.out > report1.txt

# 更改源文件中子方法，重新编译
# 编译以merge2为子方法的排序
g++ -pg -O1 $path/MergeSort_prof.cpp -o merge2
# 运行(将产生gmon.out文件)
merge2
# Profiling
gprof -b merge2 $path/gmon.out > report2.txt
