#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gamelist_bin_generator.py
将 gamelist.txt (GBK编码) 预编译为 gamelist.bin 二进制查找表文件。

使用方法:
    python gamelist_bin_generator.py [gamelist.txt路径] [输出bin路径]

默认读取当前目录的 gamelist.txt，输出到当前目录的 gamelist.bin。

二进制文件格式 (版本1):
    [4字节 Magic: 'C','G','L','T']
    [4字节 Version (uint32_t LE) = 1]
    [4字节 Entry Count (uint32_t LE)]
    每条记录:
      [2字节 Key长度 (uint16_t LE)]
      [Key字节 (UTF-8)]
      [2字节 Value长度 (uint16_t LE)]
      [Value字节 (UTF-8)]
"""

import struct
import sys
import os


def generate_bin(input_path, output_path):
    """读取 gamelist.txt 并生成 gamelist.bin"""
    
    # 读取文本文件 (GBK编码)
    with open(input_path, 'r', encoding='gbk') as f:
        content = f.read()
    
    lines = content.split('\n')
    
    # 解析数据行
    entries = []
    for line in lines:
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        if '\t' in line:
            key, value = line.split('\t', 1)
            entries.append((key.strip(), value.strip()))
    
    print(f"解析到 {len(entries)} 条游戏名称映射")
    
    # 写入二进制文件
    with open(output_path, 'wb') as f:
        # 文件头
        f.write(b'CGLT')                          # Magic
        f.write(struct.pack('<I', 1))              # Version = 1
        f.write(struct.pack('<I', len(entries)))   # Entry count
        
        # 每条记录
        for key, value in entries:
            key_bytes = key.encode('utf-8')
            value_bytes = value.encode('utf-8')
            
            if len(key_bytes) > 0xFFFF or len(value_bytes) > 0xFFFF:
                print(f"警告: 条目过长跳过 - key={key[:20]}")
                continue
            
            f.write(struct.pack('<H', len(key_bytes)))
            f.write(key_bytes)
            f.write(struct.pack('<H', len(value_bytes)))
            f.write(value_bytes)
    
    file_size = os.path.getsize(output_path)
    txt_size = os.path.getsize(input_path)
    print(f"二进制文件已生成: {output_path}")
    print(f"  二进制大小: {file_size} 字节")
    print(f"  原始文本大小: {txt_size} 字节")
    print(f"  条目数: {len(entries)}")
    
    return output_path


if __name__ == '__main__':
    if len(sys.argv) > 1:
        input_file = sys.argv[1]
    else:
        input_file = 'gamelist.txt'
    
    if len(sys.argv) > 2:
        output_file = sys.argv[2]
    else:
        output_file = 'gamelist.bin'
    
    if not os.path.exists(input_file):
        print(f"错误: 找不到文件 {input_file}")
        sys.exit(1)
    
    generate_bin(input_file, output_file)
