# OnePlayer

A simple player based on Qt(UI), FFmpeg(decode) and SDL(rendering video).

一个简(~~练~~)陋(~~手~~)的视频播放器，实现了一些基本的播放和控制功能。已知的bug似乎大多与SDL和Qt的冲突相关：

- 播放列表打开关闭会闪烁，且关闭时有可能仍会残留黑框遮挡界面
- 直接放置在视频界面上的控件样式会失效，该点目前使用一个智障方法解决(~~自我欺骗~~)：把播放控制栏定义成弹出式的顶级窗口，通过调整控制栏的位置和大小，使得它**看起来**就像是依附在父窗口里面的普通控件而不是弹出式窗口
- 播放网络视频有时会极其卡顿
- 可能还有其他的，不过忘了，因为是比较久之前做的了……