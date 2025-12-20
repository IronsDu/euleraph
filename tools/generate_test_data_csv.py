import pandas as pd
import random
import os

# --- 配置参数 ---
total_nodes = 200000  # 节点数量
min_edges = 5
max_edges = 10
file_name = "large_test_data_multi_edges.csv"  # 改为 .csv

# 如果文件已存在则删除，防止多次运行导致数据重复追加
if os.path.exists(file_name):
    os.remove(file_name)

def generate_nodes_batch(nodes_count, current_offset):
    """生成一批节点"""
    nodes = []
    for i in range(nodes_count):
        actual_id = current_offset + i
        nodes.append(
            {
                "id": f"node_{actual_id:07d}",
                "label": random.choice(["User", "Company", "Device"]),
            }
        )
    return nodes

def generate_edges_for_batch(batch_nodes, batch_start_id):
    """为一批节点生成关系"""
    edges = []
    for node in batch_nodes:
        start_id = node["id"]
        start_label = node["label"]
        num_edges = random.randint(min_edges, max_edges)

        for _ in range(num_edges):
            edges.append(
                {
                    "startId": start_id,
                    "startLabel": start_label,
                    "edgeLabel": random.choice(["FOLLOWS", "WORKS_AT", "OWNS", "BUYS"]),
                    "endId": f"target_{random.randint(0, total_nodes - 1):07d}",
                    "endLabel": random.choice(["User", "Product", "Brand"]),
                }
            )
    return edges

print("开始批量生成 CSV 数据...")

# 分批处理设置
batch_size = 10000   # 每次生成的节点数
file_chunk = []
row_count = 0
is_first_write = True # 用于控制是否写入表头

for i in range(0, total_nodes, batch_size):
    # 1. 生成当前批次的节点
    nodes_batch = generate_nodes_batch(min(batch_size, total_nodes - i), i)

    # 2. 为这批节点生成关系
    edges_batch = generate_edges_for_batch(nodes_batch, i)
    file_chunk.extend(edges_batch)
    row_count += len(edges_batch)

    # 3. 每当积累了一定数量的数据，就写入 CSV
    # 对于 CSV 来说，直接追加写入非常快
    if len(file_chunk) >= 100000 or (i + batch_size >= total_nodes):
        df_chunk = pd.DataFrame(file_chunk)
        
        # 使用 mode='a' (append) 追加模式
        # header 仅在第一次写入时为 True
        df_chunk.to_csv(
            file_name, 
            mode='a', 
            index=False, 
            header=is_first_write, 
            encoding='utf-8'
        )
        
        is_first_write = False # 后续批次不再写表头
        file_chunk = []  # 清空缓存
        
        print(f"进度：已处理节点 {min(i + batch_size, total_nodes)}/{total_nodes}, 当前总关系数: {row_count:,}")

print(f"\n--- 生成成功 ---")
print(f"文件名: {file_name}")
print(f"总计行数: {row_count:,}")