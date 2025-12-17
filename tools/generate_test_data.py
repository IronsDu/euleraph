import pandas as pd
import random

# 配置参数
total_rows = 1000000  # 100万行
file_name = "large_test_data.xlsx"

def generate_test_data(rows):
    """
    使用生成器产生数据，节省内存
    """
    for i in range(rows):
        yield {
            "startId": f"node_{i:07d}",
            "startLabel": random.choice(["User", "Company", "Device"]),
            "edgeLabel": random.choice(["FOLLOWS", "WORKS_AT", "OWNS", "BUYS"]),
            "endId": f"target_{random.randint(1, 100000):07d}",
            "endLabel": random.choice(["User", "Product", "Brand"])
        }

print(f"开始生成并写入 {total_rows} 行数据...")

# 分块处理数据，避免内存一次性撑爆
# 我们每次处理 100,000 行
chunk_size = 100000
data_gen = generate_test_data(total_rows)

with pd.ExcelWriter(file_name, engine='xlsxwriter') as writer:
    for i in range(0, total_rows, chunk_size):
        # 获取当前分块的数据
        current_chunk = [next(data_gen) for _ in range(min(chunk_size, total_rows - i))]
        df_chunk = pd.DataFrame(current_chunk)
        
        # 写入 Excel（如果是第一次写入则包含表头，否则追加）
        df_chunk.to_excel(writer, index=False, startrow=i, header=(i == 0))
        print(f"已完成: {i + len(current_chunk)} / {total_rows}")

print(f"成功生成文件: {file_name}")