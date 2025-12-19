import pandas as pd
import random
import numpy as np

print("开始批量生成数据...")
# 配置参数
total_nodes = 200000  # 节点数量
min_edges = 5
max_edges = 10
file_name = "large_test_data_multi_edges.xlsx"


def generate_nodes_batch(nodes_count):
    """生成一批节点"""
    nodes = []
    for i in range(nodes_count):
        nodes.append(
            {
                "id": f"node_{i:07d}",
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

        # 为每个节点生成5-10个关系
        num_edges = random.randint(min_edges, max_edges)

        for _ in range(num_edges):
            edges.append(
                {
                    "startId": start_id,
                    "startLabel": start_label,
                    "edgeLabel": random.choice(["FOLLOWS", "WORKS_AT", "OWNS", "BUYS"]),
                    "endId": f"target_{random.randint(batch_start_id, batch_start_id + len(batch_nodes) - 1):07d}",
                    "endLabel": random.choice(["User", "Product", "Brand"]),
                }
            )

    return edges


print("开始批量生成数据...")

# 分批处理
batch_size = 10000  # 每次处理10000个节点
chunk_size = 100000  # 每次写入100000行
file_chunk = []

with pd.ExcelWriter(file_name, engine="xlsxwriter") as writer:
    row_count = 0
    write_position = 0

    for i in range(0, total_nodes, batch_size):
        # 生成当前批次的节点
        nodes_batch = generate_nodes_batch(min(batch_size, total_nodes - i))

        # 为这批节点生成关系
        edges_batch = generate_edges_for_batch(nodes_batch, i)

        # 添加到文件块
        file_chunk.extend(edges_batch)
        row_count += len(edges_batch)

        print(
            f"已生成节点: {min(i + batch_size, total_nodes)}/{total_nodes}, 关系: {row_count:,}"
        )

        # 如果文件块达到chunk_size，写入Excel
        if len(file_chunk) >= chunk_size or (
            i + batch_size >= total_nodes and file_chunk
        ):
            df_chunk = pd.DataFrame(file_chunk)
            df_chunk.to_excel(
                writer,
                index=False,
                startrow=write_position,
                header=(write_position == 0),
            )
            write_position += len(file_chunk)
            file_chunk = []  # 清空文件块

    print(f"成功生成文件: {file_name}")
    print(f"总行数: {row_count:,}")