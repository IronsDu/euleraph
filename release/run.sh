#!/bin/bash

# 打印使用帮助的函数
usage() {
    echo "用法: $0 -f <文件路径>"
    echo "示例: $0 -f ../filepath"
    exit 1
}

# 检查是否没有提供任何参数
if [ $# -eq 0 ]; then
    usage
fi

# 使用 getopts 解析 -f 参数
# "f:" 表示 f 后面必须跟着一个参数值
while getopts "f:" opt; do
  case $opt in
    f)
      FILEPATH=$OPTARG
      ;;
    \?)
      # 处理无效选项
      usage
      ;;
  esac
done

# --- 验证逻辑 ---

# 1. 检查是否设置了 FILEPATH
if [ -z "$FILEPATH" ]; then
    echo "错误: 必须提供文件路径 (-f)"
    usage
fi

# 2. 检查路径是否存在 (可选，但建议增加)
if [ ! -e "$FILEPATH" ]; then
    echo "错误: 路径 '$FILEPATH' 不存在。"
    exit 1
fi

# --- 运行你的程序 ---

echo "正在处理路径: $FILEPATH"

./euleraph --database_dir=./my_db --need_import --data_path="$FILEPATH" --csv_row_num=800000000