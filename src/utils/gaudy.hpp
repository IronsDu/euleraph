#pragma once

#include <random>

// 随机颜色函数：产生紫色系的 RGB 值
static void set_random_purple_color()
{
    static std::mt19937             gen(std::random_device{}());
    std::uniform_int_distribution<> r_dist(100, 255);
    std::uniform_int_distribution<> g_dist(0, 80);
    std::uniform_int_distribution<> b_dist(150, 255);
    std::cout << "\033[38;2;" << r_dist(gen) << ";" << g_dist(gen) << ";" << b_dist(gen) << "m";
}

static void play_neon_banner(const std::string logo)
{
    // 隐藏光标，提升动画体验
    std::cout << "\033[?25l\033[2J";

    std::vector<std::string> lines;
    std::stringstream        ss(logo);
    std::string              line;
    size_t                   max_width = 0;
    while (std::getline(ss, line))
    {
        if (line.empty() && lines.empty())
            continue;
        lines.push_back(line);
        max_width = std::max(max_width, line.length());
    }

    // --- 阶段一：生长 + 渐影 ---
    for (int i = 0; i <= 50; ++i)
    {
        int r = std::min(40 + i * 4, 255);
        int g = std::min(i * 2, 120);
        int b = std::min(40 + i * 4, 255);

        size_t cur_width = (i * max_width) / 50;

        std::cout << "\033[H\033[1;38;2;" << r << ";" << g << ";" << b << "m";
        for (const auto& l : lines)
        {
            std::string sub = (cur_width < l.length()) ? l.substr(0, cur_width) : l;
            // 补齐空格防止残影
            std::cout << sub << std::string(max_width - sub.length(), ' ') << "\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    // --- 阶段二：斑斓闪烁 ---
    for (int blink = 0; blink < 6; ++blink)
    {
        std::cout << "\033[H"; // 移至左上角

        for (const auto& l : lines)
        {
            for (char c : l)
            {
                if (c != ' ' && c != '\n' && c != '\r')
                {
                    // 给每个字符随机颜色
                    set_random_purple_color();
                    std::cout << c;
                }
                else
                {
                    std::cout << ' ';
                }
            }
            std::cout << "\n";
        }
        std::cout << std::flush;

        // 闪烁频率：先快后慢感
        std::this_thread::sleep_for(std::chrono::milliseconds(100 + blink * 20));

        // 间隔：短暂熄灭或变暗（可选）
        if (blink % 2 == 0)
        {
            std::cout << "\033[H\033[38;2;40;0;40m"; // 变暗
            for (const auto& l : lines)
                std::cout << l << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // --- 阶段三：恢复统一的紫色 ---
    std::cout << "\033[H\033[1;38;2;255;100;255m"; // 最终的高亮紫
    for (const auto& l : lines)
    {
        std::cout << l << "\n";
    }
    std::cout << "\033[0m" << std::flush;

    // 恢复光标
    std::cout << "\033[?25h" << std::endl;
}

class NeonArrowBar
{
public:
    NeonArrowBar(const std::string& label, int total, int width = 40) : label_(label), total_(total), bar_width_(width)
    {
        std::cout << "\033[?25l"; // 隐藏光标
    }

    ~NeonArrowBar()
    {
        finish();
    }

    void update(int current, const std::string& status = "")
    {
        current          = std::min(current, total_);
        float percentage = static_cast<float>(current) / total_;
        int   pos        = static_cast<int>(bar_width_ * percentage);

        std::cout << "\r"; // 回到行首

        // 1. 标签与百分比 (紫色)
        std::cout << "\033[38;2;200;100;255m" << label_ << " [" << static_cast<int>(percentage * 100) << "%] ";

        // 2. 进度条左外框 (深灰色)
        std::cout << "\033[38;5;240m[";

        // 3. 绘制进度条体
        for (int j = 0; j < bar_width_; ++j)
        {
            if (j < pos)
            {
                // 已填充路径：使用等号 '='，带有紫色渐变
                int r = 80 + (j * 175 / bar_width_);
                std::cout << "\033[38;2;" << r << ";0;255m=";
            }
            else if (j == pos && current < total_)
            {
                // ======= 文本箭头组合 =======
                // 我们用 "==>" 表示箭头。
                // 亮红色尖端 \033[1;31m
                if (j <= bar_width_ - 3)
                {
                    std::cout << "\033[1;38;2;255;100;255m==\033[1;31m>\033[0m";
                    j += 2; // 补偿长度：因为多印了两个字符
                }
                else
                {
                    // 如果接近末尾，空间不足，仅打印单个尖端
                    std::cout << "\033[1;31m>\033[0m";
                }
            }
            else
            {
                // 未填充空白
                std::cout << " ";
            }
        }

        // 4. 进度条右外框 (深灰色)
        std::cout << "\033[38;5;240m]";

        // 5. 状态信息 + 清除行末残余
        if (!status.empty())
        {
            std::cout << " \033[38;2;150;150;150m" << status << "\033[K";
        }
        else
        {
            std::cout << "\033[K";
        }

        std::cout << std::flush;
    }

    void finish()
    {
        if (!finished_)
        {
            update(total_, "Done.");
            std::cout << "\033[?25h" << std::endl; // 恢复光标并换行
            finished_ = true;
        }
    }

private:
    std::string label_;
    int         total_;
    int         bar_width_;
    bool        finished_ = false;
};