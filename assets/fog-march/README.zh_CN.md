<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 迷雾三国 Sprite 素材

迷雾三国战棋应用(`feature/fog-march`,见 `docs/PRD_FOG_MARCH.zh_CN.md`
第 10.7 节)的像素风 sprite。全部美术为本仓库原创,以生成脚本内的 ASCII
像素图为唯一来源,不使用任何外部图片。许可:MIT,与仓库一致。

## 素材清单

共 18 张,每张 26 × 26(战场一格):

| 组 | 文件 | 格式 |
| --- | --- | --- |
| 地形,视野内 | `fog_terrain_{plain,mountain,forest,river,city}.rgb565` | 原始小端 RGB565,不透明 |
| 地形,已探索迷雾变体 | 同名加 `_dim` 后缀 | 同上;低饱和 + 变暗色板 |
| 我方单位(蓝) | `fog_unit_{spear,archer,cavalry,general}_blue.rgb565a8` | RGB565 平面 + A8 透明平面 |
| 敌方单位(红) | 同名加 `_red` 后缀 | 同上 |

合计 29,744 字节 Flash(10 张地形 × 1,352 B + 8 张单位 × 2,028 B)。

原始数据位于 `main/assets/`(与 `demo/rock-paper-scissors` 分支布局一致),
经 CMake `EMBED_FILES` 嵌入固件,运行时包装为 `lv_image_dsc_t`
(`LV_COLOR_FORMAT_RGB565` / `LV_COLOR_FORMAT_RGB565A8`)。

## 再生成

```bash
python3 tools/gen_fog_march_assets.py
```

`tools/gen_fog_march_assets.py` 内的 ASCII 像素图是唯一事实来源。修改图或
色板后重跑生成器,并将刷新后的 `main/assets/` 数据与脚本一并提交。
`tests/test_fog_assets.py`(已接入 `tools/validate.sh --static`)会在提交的
数据或预览与全新再生成不一致时报错。

## 预览

`preview/` 下每张 sprite 一个 PNG(单位合成在平地格上),另有
`fog_march_contact_sheet.png`(地形明/暗两行 + 蓝/红单位两行)供目视评审。
预览是生成产物,不是编辑源。
