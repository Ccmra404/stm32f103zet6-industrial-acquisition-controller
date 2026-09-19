from pathlib import Path
from html import escape

import cairosvg
from PIL import Image


ROOT = Path(__file__).resolve().parent
IMAGE_DIR = ROOT / "images"
SVG_DIR = ROOT / ".render"

FONT = "Microsoft YaHei, Noto Sans SC, sans-serif"
MONO = "Cascadia Mono, Consolas, monospace"

COLORS = {
    "ink": "#17324D",
    "muted": "#5C7185",
    "line": "#C8D4DF",
    "paper": "#F5F8FB",
    "white": "#FFFFFF",
    "blue": "#2F6FB0",
    "blue_fill": "#E8F1FA",
    "teal": "#278377",
    "teal_fill": "#E6F4F1",
    "amber": "#B87516",
    "amber_fill": "#FBF1DF",
    "red": "#B14E4A",
    "red_fill": "#F8E9E8",
    "violet": "#6E5AA8",
    "violet_fill": "#EFECF8",
    "slate": "#58708A",
    "slate_fill": "#EDF2F6",
}


def add_text(
    parts,
    x,
    y,
    text,
    size=24,
    fill=None,
    weight=400,
    anchor="start",
    font=FONT,
    opacity=1,
):
    fill = fill or COLORS["ink"]
    parts.append(
        f'<text x="{x}" y="{y}" font-family="{font}" font-size="{size}" '
        f'font-weight="{weight}" fill="{fill}" text-anchor="{anchor}" '
        f'opacity="{opacity}">{escape(text)}</text>'
    )


def add_rect(
    parts,
    x,
    y,
    w,
    h,
    fill,
    stroke="none",
    radius=18,
    stroke_width=1.5,
    opacity=1,
):
    parts.append(
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" '
        f'fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width}" '
        f'opacity="{opacity}"/>'
    )


def add_line(
    parts,
    x1,
    y1,
    x2,
    y2,
    color=None,
    width=2,
    dash=None,
    marker=False,
):
    color = color or COLORS["line"]
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    marker_attr = f' marker-end="url(#{marker_id(color)})"' if marker else ""
    parts.append(
        f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
        f'stroke="{color}" stroke-width="{width}"{dash_attr}{marker_attr}/>'
    )


def add_path(parts, d, color, width=3, marker=False, fill="none", opacity=1):
    marker_attr = f' marker-end="url(#{marker_id(color)})"' if marker else ""
    parts.append(
        f'<path d="{d}" fill="{fill}" stroke="{color}" stroke-width="{width}" '
        f'stroke-linecap="round" stroke-linejoin="round" opacity="{opacity}"'
        f'{marker_attr}/>'
    )


def marker_id(color):
    if color == COLORS["amber"]:
        return "arrow-amber"
    if color == COLORS["teal"]:
        return "arrow-teal"
    if color == COLORS["violet"]:
        return "arrow-violet"
    return "arrow-blue"


def add_badge(parts, x, y, text, fill, text_fill=None, width=None):
    width = width or max(86, len(text) * 22 + 28)
    add_rect(parts, x, y, width, 40, fill, radius=20)
    add_text(
        parts,
        x + width / 2,
        y + 27,
        text,
        19,
        text_fill or COLORS["ink"],
        weight=500,
        anchor="middle",
    )


def add_card(parts, x, y, w, h, title, lines, color, fill, index=None):
    add_rect(parts, x, y, w, h, COLORS["white"], COLORS["line"], radius=16)
    add_rect(parts, x, y, 8, h, color, radius=4)
    title_y = y + 39
    if index:
        add_text(parts, x + 25, title_y, index, 16, color, weight=500, font=MONO)
        title_x = x + 66
    else:
        title_x = x + 25
    add_text(parts, title_x, title_y, title, 25, COLORS["ink"], weight=500)
    yy = y + 73
    for line in lines:
        parts.append(
            f'<circle cx="{x + 31}" cy="{yy - 7}" r="4" fill="{color}"/>'
        )
        add_text(parts, x + 48, yy, line, 20, COLORS["muted"])
        yy += 30


def header(parts, width, title, subtitle):
    add_text(parts, 72, 72, title, 40, COLORS["ink"], weight=500)
    add_text(parts, 72, 110, subtitle, 21, COLORS["muted"])
    add_line(parts, 72, 135, width - 72, 135, COLORS["line"], 1.5)


def svg_document(width, height, title, body):
    defs = f"""
    <defs>
      <marker id="arrow-blue" markerWidth="11" markerHeight="11" refX="9" refY="5.5"
              orient="auto" markerUnits="strokeWidth">
        <path d="M0,0 L11,5.5 L0,11 z" fill="{COLORS["blue"]}"/>
      </marker>
      <marker id="arrow-amber" markerWidth="11" markerHeight="11" refX="9" refY="5.5"
              orient="auto" markerUnits="strokeWidth">
        <path d="M0,0 L11,5.5 L0,11 z" fill="{COLORS["amber"]}"/>
      </marker>
      <marker id="arrow-teal" markerWidth="11" markerHeight="11" refX="9" refY="5.5"
              orient="auto" markerUnits="strokeWidth">
        <path d="M0,0 L11,5.5 L0,11 z" fill="{COLORS["teal"]}"/>
      </marker>
      <marker id="arrow-violet" markerWidth="11" markerHeight="11" refX="9" refY="5.5"
              orient="auto" markerUnits="strokeWidth">
        <path d="M0,0 L11,5.5 L0,11 z" fill="{COLORS["violet"]}"/>
      </marker>
      <filter id="shadow" x="-10%" y="-10%" width="120%" height="130%">
        <feDropShadow dx="0" dy="5" stdDeviation="8" flood-color="#17324D"
                      flood-opacity="0.08"/>
      </filter>
    </defs>
    """
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
        f'height="{height}" viewBox="0 0 {width} {height}" role="img" '
        f'aria-labelledby="title desc">'
        f"<title id=\"title\">{escape(title)}</title>"
        f"<desc id=\"desc\">工业采集控制终端项目架构说明图</desc>"
        f"{defs}"
        f'<rect width="{width}" height="{height}" fill="{COLORS["paper"]}"/>'
        f"{''.join(body)}"
        f"</svg>"
    )


def render(name, width, height, svg):
    SVG_DIR.mkdir(parents=True, exist_ok=True)
    IMAGE_DIR.mkdir(parents=True, exist_ok=True)
    svg_path = SVG_DIR / f"{name}.svg"
    png_path = SVG_DIR / f"{name}.png"
    webp_path = IMAGE_DIR / f"{name}.webp"
    svg_path.write_text(svg, encoding="utf-8")
    cairosvg.svg2png(
        bytestring=svg.encode("utf-8"),
        write_to=str(png_path),
        output_width=width,
        output_height=height,
    )
    with Image.open(png_path) as image:
        image.convert("RGB").save(webp_path, "WEBP", quality=92, method=6)


def system_architecture():
    width, height = 1800, 1050
    parts = []
    header(
        parts,
        width,
        "工业采集控制终端 · 系统架构",
        "现场信号经过保护与隔离后进入 STM32，ESP32-S3 承担显示、联网和远程交互",
    )

    band_y, band_h = 164, 792
    bands = [
        (52, 356, "现场侧", "仪表 / 执行器 / 总线", COLORS["amber"], COLORS["amber_fill"]),
        (432, 356, "保护与隔离", "调理 / 隔离 / 驱动", COLORS["red"], COLORS["red_fill"]),
        (836, 420, "控制器侧", "实时控制 / 应用处理", COLORS["blue"], COLORS["blue_fill"]),
        (1280, 468, "应用侧", "显示 / 网络 / 音频", COLORS["teal"], COLORS["teal_fill"]),
    ]
    for x, w, title, subtitle, color, fill in bands:
        add_rect(parts, x, band_y, w, band_h, fill, radius=24)
        add_text(parts, x + 28, band_y + 43, title, 26, color, weight=500)
        add_text(parts, x + 28, band_y + 73, subtitle, 18, COLORS["muted"])

    field_x, field_w = 72, 316
    add_card(
        parts,
        field_x,
        band_y + 100,
        field_w,
        118,
        "过程量输入",
        ["0 到 10V / 4 到 20mA"],
        COLORS["amber"],
        COLORS["amber_fill"],
        "01",
    )
    add_card(
        parts,
        field_x,
        band_y + 238,
        field_w,
        118,
        "温度与触点",
        ["PT100 / PT1000", "24V 干接点 × 8"],
        COLORS["amber"],
        COLORS["amber_fill"],
        "02",
    )
    add_card(
        parts,
        field_x,
        band_y + 376,
        field_w,
        118,
        "现场总线",
        ["RS485 / RS232 / CAN"],
        COLORS["amber"],
        COLORS["amber_fill"],
        "03",
    )
    add_card(
        parts,
        field_x,
        band_y + 514,
        field_w,
        138,
        "执行器与电源",
        ["继电器负载 × 8", "24V DC 输入"],
        COLORS["amber"],
        COLORS["amber_fill"],
        "04",
    )

    iso_x, iso_w = 452, 316
    add_card(
        parts,
        iso_x,
        band_y + 100,
        iso_w,
        118,
        "模拟前端",
        ["RC 滤波 + ADS1256", "ADR421 精密基准"],
        COLORS["red"],
        COLORS["red_fill"],
        "05",
    )
    add_card(
        parts,
        iso_x,
        band_y + 238,
        iso_w,
        118,
        "RTD 保护",
        ["MAX31865 + TVS"],
        COLORS["red"],
        COLORS["red_fill"],
        "06",
    )
    add_card(
        parts,
        iso_x,
        band_y + 376,
        iso_w,
        118,
        "数字隔离",
        ["TLP291-4 光耦", "ULN2803 驱动"],
        COLORS["red"],
        COLORS["red_fill"],
        "07",
    )
    add_card(
        parts,
        iso_x,
        band_y + 514,
        iso_w,
        138,
        "隔离接口",
        ["TD541 系列收发器", "共模电感与端接"],
        COLORS["red"],
        COLORS["red_fill"],
        "08",
    )

    ctrl_x = 856
    add_rect(parts, ctrl_x, band_y + 100, 380, 300, COLORS["white"], COLORS["line"], 18)
    add_badge(parts, ctrl_x + 24, band_y + 122, "实时控制域", COLORS["blue_fill"], COLORS["blue"], 174)
    add_text(parts, ctrl_x + 24, band_y + 211, "STM32F103ZET6", 31, COLORS["ink"], weight=500)
    add_text(parts, ctrl_x + 24, band_y + 244, "Cortex-M3 · 裸机 / RTOS", 19, COLORS["muted"])
    stm_lines = [
        "ADS1256 · SPI2  ·  8 通道 24 位",
        "MAX31865 · SPI3  ·  PT100 / PT1000",
        "AT24C32D · I2C2  ·  参数与校准",
        "继电器 / 光耦 / DAC / ADC 监测",
    ]
    for i, line in enumerate(stm_lines):
        yy = band_y + 290 + i * 30
        parts.append(f'<circle cx="{ctrl_x + 32}" cy="{yy - 7}" r="4" fill="{COLORS["blue"]}"/>')
        add_text(parts, ctrl_x + 50, yy, line, 19, COLORS["muted"])

    bridge_y = band_y + 430
    add_line(parts, ctrl_x + 190, band_y + 400, ctrl_x + 190, bridge_y, COLORS["violet"], 3)
    add_badge(parts, ctrl_x + 102, bridge_y - 5, "UART 协议", COLORS["violet_fill"], COLORS["violet"], 176)
    add_text(parts, ctrl_x + 190, bridge_y + 58, "采集数据 / 状态 / 控制命令", 18, COLORS["muted"], anchor="middle")

    add_rect(parts, ctrl_x, band_y + 500, 380, 252, COLORS["white"], COLORS["line"], 18)
    add_badge(parts, ctrl_x + 24, band_y + 522, "交互与网络域", COLORS["teal_fill"], COLORS["teal"], 174)
    add_text(parts, ctrl_x + 24, band_y + 610, "ESP32-S3-N16R8", 31, COLORS["ink"], weight=500)
    add_text(parts, ctrl_x + 24, band_y + 643, "WiFi · LCD · 音频 · OTA", 19, COLORS["muted"])
    esp_lines = [
        "ST7789 LCD · SPI",
        "ES8311 + NS4150B · I2C / I2S",
        "MQTT / WebSocket / 云端接入",
        "远程配置、事件上报和在线升级",
    ]
    for i, line in enumerate(esp_lines):
        yy = band_y + 685 + i * 28
        parts.append(f'<circle cx="{ctrl_x + 32}" cy="{yy - 7}" r="4" fill="{COLORS["teal"]}"/>')
        add_text(parts, ctrl_x + 50, yy, line, 18, COLORS["muted"])

    app_x, app_w = 1300, 428
    add_rect(
        parts,
        app_x,
        band_y + 100,
        app_w,
        290,
        COLORS["white"],
        COLORS["line"],
        18,
    )
    add_text(parts, app_x + 26, band_y + 144, "本地人机界面", 29, COLORS["ink"], weight=500)
    add_text(parts, app_x + 26, band_y + 181, "LCD 实时状态、报警与参数配置", 19, COLORS["muted"])
    ui_boxes = [
        ("LCD", "1.54 英寸 / ST7789", COLORS["teal_fill"], COLORS["teal"]),
        ("音频", "语音提示与交互", COLORS["violet_fill"], COLORS["violet"]),
        ("按键", "BOOT / 复位 / 扩展", COLORS["slate_fill"], COLORS["ink"]),
    ]
    for i, (title, subtitle, fill, color) in enumerate(ui_boxes):
        bx = app_x + 26 + i * 128
        add_rect(parts, bx, band_y + 222, 112, 124, fill, radius=14)
        add_text(parts, bx + 56, band_y + 266, title, 22, color, weight=500, anchor="middle")
        add_text(parts, bx + 56, band_y + 300, subtitle, 15, color, anchor="middle")

    add_rect(
        parts,
        app_x,
        band_y + 430,
        app_w,
        322,
        COLORS["white"],
        COLORS["line"],
        18,
    )
    add_text(parts, app_x + 26, band_y + 474, "网络与远程应用", 29, COLORS["ink"], weight=500)
    add_text(parts, app_x + 26, band_y + 511, "设备不依赖云端完成本地采集与控制", 19, COLORS["muted"])
    cloud_items = [
        ("WiFi", "2.4GHz 网络接入"),
        ("MQTT", "状态上报和命令下发"),
        ("Web", "浏览器看板与历史记录"),
        ("OTA", "远程固件升级"),
    ]
    for i, (title, subtitle) in enumerate(cloud_items):
        bx = app_x + 26 + (i % 2) * 184
        by = band_y + 552 + (i // 2) * 80
        add_rect(parts, bx, by, 164, 62, COLORS["teal_fill"], radius=13)
        add_text(parts, bx + 16, by + 26, title, 19, COLORS["teal"], weight=500)
        add_text(parts, bx + 16, by + 49, subtitle, 15, COLORS["muted"])

    add_line(parts, 408, band_y + 432, 432, band_y + 432, COLORS["amber"], 5, marker=True)
    add_line(parts, 788, band_y + 432, 816, band_y + 432, COLORS["red"], 5, marker=True)
    add_line(parts, 1256, band_y + 432, 1280, band_y + 432, COLORS["teal"], 5, marker=True)

    footer_y = 987
    add_rect(parts, 52, footer_y - 35, 1696, 54, COLORS["white"], COLORS["line"], 14)
    add_text(
        parts,
        72,
        footer_y,
        "设计原则：STM32 保持硬实时路径；ESP32-S3 隔离网络任务；现场接口独立供电并经过保护、隔离和滤波。",
        19,
        COLORS["muted"],
    )

    svg = svg_document(width, height, "系统架构", parts)
    render("arch-system", width, height, svg)


def data_flow():
    width, height = 1800, 820
    parts = []
    header(
        parts,
        width,
        "采集、控制与远程交互 · 数据流",
        "蓝色为上行状态链路，橙色为下行控制链路，绿色表示本地安全逻辑不依赖网络",
    )

    stages = [
        ("01", "现场信号", ["AI / RTD / DI", "485 / 232 / CAN"], COLORS["amber"]),
        ("02", "调理与隔离", ["滤波 / TVS / 光耦", "隔离电源与驱动"], COLORS["red"]),
        ("03", "STM32", ["采样、标定、报警", "继电器与 DAC 输出"], COLORS["blue"]),
        ("04", "ESP32-S3", ["LCD、音频、WiFi", "MQTT、Web、OTA"], COLORS["teal"]),
        ("05", "用户与云端", ["实时状态与报警", "配置和控制命令"], COLORS["violet"]),
    ]
    x0, gap, card_w, card_h = 62, 34, 310, 235
    for i, (index, title, lines, color) in enumerate(stages):
        x = x0 + i * (card_w + gap)
        y = 230
        add_rect(parts, x, y, card_w, card_h, COLORS["white"], COLORS["line"], 18)
        add_rect(parts, x, y, card_w, 8, color, radius=4)
        add_text(parts, x + 24, y + 50, index, 17, color, weight=500, font=MONO)
        add_text(parts, x + 70, y + 52, title, 29, COLORS["ink"], weight=500)
        for j, line in enumerate(lines):
            yy = y + 106 + j * 38
            add_rect(parts, x + 24, yy - 23, card_w - 48, 34, COLORS["slate_fill"], radius=10)
            add_text(parts, x + 40, yy, line, 19, COLORS["muted"])
        if i < len(stages) - 1:
            add_line(parts, x + card_w + 7, y + card_h / 2, x + card_w + gap - 7, y + card_h / 2, COLORS["blue"], 5, marker=True)

    add_badge(parts, 440, 170, "上行：采集数据 / 设备状态 / 报警事件", COLORS["blue_fill"], COLORS["blue"], 418)
    add_text(parts, 1438, 548, "命令与配置", 18, COLORS["amber"], anchor="middle")
    add_text(parts, 1062, 548, "输出控制与 OTA", 18, COLORS["amber"], anchor="middle")
    add_path(parts, "M 1510 478 C 1510 585, 1360 585, 1360 478", COLORS["amber"], 5, marker=True)
    add_path(parts, "M 1195 478 C 1195 585, 935 585, 935 478", COLORS["amber"], 5, marker=True)

    add_rect(parts, 168, 645, 650, 104, COLORS["teal_fill"], radius=18)
    add_text(parts, 198, 684, "本地控制闭环", 25, COLORS["teal"], weight=500)
    add_text(parts, 198, 720, "网络异常时，STM32 继续执行采样、报警和继电器安全策略。", 19, COLORS["muted"])
    add_path(parts, "M 818 696 C 900 696, 935 620, 935 482", COLORS["teal"], 5, marker=True)

    add_rect(parts, 1115, 645, 625, 104, COLORS["white"], COLORS["line"], 18)
    add_text(parts, 1143, 684, "数据一致性", 25, COLORS["ink"], weight=500)
    add_text(parts, 1143, 720, "UART 使用固定帧头、长度、序号和 CRC；显示数据来自统一状态快照。", 19, COLORS["muted"])

    svg = svg_document(width, height, "数据流", parts)
    render("arch-data-flow", width, height, svg)


def io_topology():
    width, height = 1800, 1140
    parts = []
    header(
        parts,
        width,
        "控制器与外设 · 通信拓扑",
        "按总线类型整理 STM32 与 ESP32-S3 的主要连接，现场总线位于两侧",
    )

    add_rect(parts, 74, 190, 630, 836, COLORS["blue_fill"], radius=24)
    add_text(parts, 104, 236, "STM32F103ZET6", 34, COLORS["blue"], weight=500)
    add_text(parts, 104, 271, "实时采集、控制、存储和现场通信", 19, COLORS["muted"])

    add_rect(parts, 1096, 190, 630, 836, COLORS["teal_fill"], radius=24)
    add_text(parts, 1126, 236, "ESP32-S3-N16R8", 34, COLORS["teal"], weight=500)
    add_text(parts, 1126, 271, "显示、音频、联网、远程配置和 OTA", 19, COLORS["muted"])

    stm_groups = [
        (
            "高精度采集",
            "SPI2",
            ["ADS1256  24 位 ADC", "ADR421  2.5V 基准", "CS / DRDY / RESET / SYNC"],
            COLORS["red"],
        ),
        (
            "温度采集",
            "SPI3",
            ["MAX31865ATP+T", "三路 SMBJ5.0CA TVS", "C95 差分滤波"],
            COLORS["amber"],
        ),
        (
            "存储与扩展",
            "I2C2 / I2C1",
            ["AT24C32D  地址 0x50", "外部 I2C 扩展接口", "独立 4.7kΩ 上拉"],
            COLORS["violet"],
        ),
        (
            "数字量输入",
            "GPIO",
            ["TLP291-4  八路隔离", "PD2 / PG9 到 PG15", "现场侧独立地"],
            COLORS["amber"],
        ),
        (
            "输出与监测",
            "GPIO / DAC / ADC",
            ["ULN2803 + 八路继电器", "0 到 10V / 4 到 20mA", "24V 与 5V 电源监测"],
            COLORS["blue"],
        ),
        (
            "现场通信",
            "USART / CAN",
            ["USART2  RS485 + 方向脚", "USART4  RS232", "CAN1  TDH541SCANFD"],
            COLORS["teal"],
        ),
    ]
    for i, (title, bus, lines, color) in enumerate(stm_groups):
        col, row = i % 2, i // 2
        x = 104 + col * 292
        y = 322 + row * 210
        add_rect(parts, x, y, 266, 176, COLORS["white"], COLORS["line"], 16)
        add_text(parts, x + 20, y + 34, title, 23, COLORS["ink"], weight=500)
        bus_width = min(226, max(80, len(bus) * 14 + 24))
        add_badge(parts, x + 20, y + 49, bus, COLORS["slate_fill"], color, bus_width)
        for j, line in enumerate(lines):
            yy = y + 115 + j * 22
            add_text(parts, x + 20, yy, line, 17, COLORS["muted"])

    esp_groups = [
        (
            "显示",
            "SPI",
            ["ST7789  1.54 英寸 LCD", "SCK / MOSI / DC / CS", "RESET / BL"],
            COLORS["teal"],
        ),
        (
            "音频",
            "I2C / I2S",
            ["ES8311  Codec", "NS4150B  功放", "DIN / DOUT / LRCK / MCLK"],
            COLORS["violet"],
        ),
        (
            "下载与调试",
            "USB / UART0",
            ["Type-C 下载接口", "CH340K USB 转串口", "BOOT / 复位控制"],
            COLORS["slate"],
        ),
        (
            "网络与应用",
            "WiFi / MQTT",
            ["2.4GHz 天线", "MQTT / WebSocket", "OTA 与配置同步"],
            COLORS["blue"],
        ),
    ]
    for i, (title, bus, lines, color) in enumerate(esp_groups):
        col, row = i % 2, i // 2
        x = 1126 + col * 292
        y = 322 + row * 210
        add_rect(parts, x, y, 266, 176, COLORS["white"], COLORS["line"], 16)
        add_text(parts, x + 20, y + 34, title, 23, COLORS["ink"], weight=500)
        bus_width = min(226, max(80, len(bus) * 14 + 24))
        add_badge(parts, x + 20, y + 49, bus, COLORS["slate_fill"], color, bus_width)
        for j, line in enumerate(lines):
            yy = y + 115 + j * 22
            add_text(parts, x + 20, yy, line, 17, COLORS["muted"])

    add_rect(parts, 750, 446, 300, 230, COLORS["white"], COLORS["line"], 18)
    add_text(parts, 900, 492, "板间通信", 29, COLORS["violet"], weight=500, anchor="middle")
    add_badge(parts, 817, 515, "UART1", COLORS["violet_fill"], COLORS["violet"], 166)
    add_text(parts, 900, 605, "采集值与状态上行", 19, COLORS["muted"], anchor="middle")
    add_text(parts, 900, 638, "配置与控制下行", 19, COLORS["muted"], anchor="middle")
    add_path(parts, "M 704 535 C 742 535, 742 510, 750 510", COLORS["blue"], 5, marker=True)
    add_path(parts, "M 1050 612 C 1080 612, 1080 638, 1096 638", COLORS["amber"], 5, marker=True)
    add_text(parts, 725, 500, "TX", 17, COLORS["blue"], weight=500, font=MONO)
    add_text(parts, 1058, 630, "RX", 17, COLORS["amber"], weight=500, font=MONO)

    add_rect(parts, 74, 1050, 1652, 58, COLORS["white"], COLORS["line"], 14)
    add_text(
        parts,
        96,
        1087,
        "接口原则：SPI 用于高速器件，I2C 用于低速配置，UART 用于板间与现场串口，CAN 用于多节点现场总线。",
        19,
        COLORS["muted"],
    )

    svg = svg_document(width, height, "通信拓扑", parts)
    render("arch-io-topology", width, height, svg)


if __name__ == "__main__":
    system_architecture()
    data_flow()
    io_topology()
