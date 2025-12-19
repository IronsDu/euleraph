#/bin/bash

CPU_TYPE=$(uname -m) #aarch64 x86_64

#########################################################
#### 拷贝可执行文件的依赖到可执行文件所在目录，并使用patchelf修改其rpath以及ld路径
#########################################################
patchelf_file()
{
    echo "Begin patchelf file: $1 $2"
    CURRENT_FILE_DIR=$(cd `dirname $0`; pwd)

    if [ ${CPU_TYPE} = "x86_64" ]; then
        PATCHELF_PATH=patchelf
    elif [ ${CPU_TYPE} = "aarch64" ]; then
        PATCHELF_PATH=patchelf
    else 
        exit_retcode 1 "This platform is not currently supported by patchelf."
    fi

    exe_bin=$(basename $1)
    # 获取可执行文件所在目录
    EXE_DIR=$(dirname "$1")
    # 新的lib文件相对于可执行文件的路径 
    NEW_LIB_DIR="$2"
    # 创建存放lib的文件夹
    mkdir -p ${EXE_DIR}/${NEW_LIB_DIR}
    # 收集可执行文件的依赖库
    ldd "$1" | awk '{if(substr($3,0,1)=="/" || substr($3,0,1)==".") print $3}' > "tmp_ldd"
    # 将依赖库拷贝到可执行文件所在目录的NEW_LIB_DIR下
    cat "tmp_ldd" | xargs -d '\n' -I{} cp --copy-contents {} ${EXE_DIR}/${NEW_LIB_DIR}/
    rm "tmp_ldd"

	if file -b "$exe_bin" | grep -q 'executable'; then
		# 将可执行文件的ld解释器拷贝到可执行文件所在目录的NEW_LIB_DIR下，并重新命名为ld-interpreter.so
		cp --copy-contents $(${PATCHELF_PATH} --print-interpreter "$1") ${EXE_DIR}/${NEW_LIB_DIR}/ld-interpreter.so

		# 为ld解释器添加可执行权限
		chmod +x ${EXE_DIR}/${NEW_LIB_DIR}/ld-interpreter.so
		
		# 设置可执行文件ld解释器路径
		# 注意，由于此路径是相对于执行程序时的工作目录，
		# 因此，必须在可执行文件所在目录才能执行程序，无法在其他目录下直接执行程序！
		${PATCHELF_PATH} --set-interpreter ${NEW_LIB_DIR}/ld-interpreter.so "$1"
		# 设置可执行文件的rpath为$ORIGIN，它表示可执行文件所在目录
		${PATCHELF_PATH} --set-rpath '$ORIGIN':'$ORIGIN'/${NEW_LIB_DIR}/ "$1" --force-rpath
	fi
}

patchelf_file $*
exit 0
