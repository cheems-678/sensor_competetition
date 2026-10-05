/* ===========================================================================
 * 平台注入.js  ——  在 puliedu 仿真页面里运行, 把串口数据推给本机 _live.py
 * ===========================================================================
 *
 * 【怎么用】
 *   1) 电脑上先跑:  python _live.py 8765
 *   2) 浏览器打开平台, 进到你的实验页, **点「运行」让仿真跑起来**
 *   3) 按 F12 → **Console** 标签页 → 粘到面板**最底下那行 `>` 输入行**, 只粘
 *      `注入一行.js` 里的**那一行**并回车(见该文件)。
 *      ★ 千万别粘到平台页面自带的 Monaco 代码编辑框 —— 它只解析语法不解运行,
 *        多行中文注释块会直接抛 `Uncaught Error: invalid AST`。
 *   4) 终端里出现 [收数] 就是通了; 再打开 http://127.0.0.1:8765/ 看 3D 大屏
 *
 * 【它做了什么】
 *   平台前端把串口数据累加在 Vuex 的 state.newGdbg.uartStr 里
 *   (源码 app.js 的 updateUart(): o.uartStr[n.id] = o.uartStr[n.id] + "," + data,
 *    即"逗号分隔的十进制字节")。这段 JS 每 400ms 只取**新增的那一段**,
 *   POST 给本机 _live.py —— 不动平台原有逻辑, 不另开连接, 所以**不会踢掉仿真**。
 *
 * 【为什么不能另开一条 WebSocket】
 *   平台后端限制「同一 project 只允许一条仿真连接, 新连接会停掉旧的」
 *   (前端源码 cmd=1002 分支里就写着这条提示)。所以只能读页面上已有连接的数据。
 *
 * 【停止】刷新页面, 或在 Console 里执行 __wbStop()
 * =========================================================================== */

(function () {
  var PORT = 8765;                       // 改了端口这里也要跟着改
  var BASE = 'http://127.0.0.1:' + PORT;

  if (window.__wbLive) {
    console.log('%c[wb-live] 已经在跑了, 不用重复注入', 'color:#c80;font-weight:bold');
    console.log('  停止: 刷新页面, 或执行 __wbStop()');
    return;
  }
  window.__wbLive = true;

  /* ---- 取 Vuex store: Vue2 里任意组件实例都能摸到 $store ---- */
  function getStore() {
    var el = document.getElementById('app');
    var vm = el && el.__vue__;
    if (vm && vm.$store) return vm.$store;
    var all = document.querySelectorAll('div');       // 兜底: 任取一个挂了的
    for (var i = 0; i < all.length; i++) {
      var v = all[i].__vue__;
      if (v && v.$store) return v.$store;
    }
    return null;
  }

  var last = {};      // 各端口上次发到的位置
  var nSent = 0, nFail = 0, alive = false;

  function say(t, c) {
    console.log('%c[wb-live] ' + t, 'color:' + (c || '#0a0') + ';font-weight:bold');
  }

  function post(key, delta) {
    fetch(BASE + '/raw?k=' + encodeURIComponent(key), {
      method: 'POST',
      body: delta,
      headers: { 'Content-Type': 'text/plain' },
      cache: 'no-store'
    }).then(function (r) {
      if (!r.ok) throw 0;
      nSent += delta.length;
    }).catch(function () {
      nFail++;
      if (alive && nFail % 20 === 1) say('推送失败 ' + nFail + ' 次 (本机服务关了吗?)', '#c00');
    });
  }

  function tick() {
    var store = getStore();
    if (!store || !store.state || !store.state.newGdbg) return;
    /* ★★ 实测(2026-10-03): 平台 uartStr 里的格式是
     *    "{元件id: ',0X79,0X3d,0X33,0X34,...'}" —— **逗号分隔的大写十六进制**
     *    (0X79='y' 0X3d='=' 0X33='3'…), 不是十进制。按十进制读整条流会全错位。
     *    本机 _live.py 的 to_bytes() 三路都认(0Xxx / 十进制 / 明文)。 */
    var map = store.state.newGdbg.uartStr;
    if (!map) return;
    for (var k in map) {
      var full = map[k] || '';
      var prev = last[k] || 0;
      if (full.length < prev) { last[k] = 0; prev = 0; }   // 平台清过缓冲, 重新来
      if (full.length <= prev) continue;
      /* ★ 必须切在**逗号**上。这段字符串是"逗号分隔的字节值", 按字符位置硬切
         会把一个字节切成两半 —— 两边都合法, 不报错, 但整条数据全错位。
         只发到最后一个逗号为止, 剩下的留到下一拍。 */
      var cut = full.lastIndexOf(',');
      if (cut < prev) continue;                        // 还没攒够一个完整字节
      post(k, ',' + full.slice(prev, cut) + ',');      // 两端包逗号, 解析端零歧义
      last[k] = cut + 1;
    }
  }

  /* ---- 先自检本机服务通不通, 免得白等 ---- */
  say('正在自检 ' + BASE + ' ...', '#888');
  fetch(BASE + '/ping', { cache: 'no-store' })
    .then(function (r) { return r.text(); })
    .then(function () {
      alive = true;
      say('本机服务已连通, 开始推送 (每 400ms 抓一次串口增量)');
      console.log('  3D 大屏 : ' + BASE + '/');
      console.log('  状态面板: ' + BASE + '/status');
      console.log('  原始串口: ' + BASE + '/tail');
      console.log('  ★ 别忘了在平台上点「运行」, 不然串口是静的');
      window.__wbTimer = setInterval(tick, 400);
    })
    .catch(function () {
      window.__wbLive = false;
      say('连不上本机服务 ' + BASE, '#c00');
      console.log('  1) 先在电脑上跑:  python _live.py ' + PORT);
      console.log('  2) 若已经在跑仍连不上: 浏览器可能拦了 https 页面访问 127.0.0.1,');
      console.log('     在地址栏左侧的盾牌图标里允许"不安全内容", 或改用 Edge/Chrome 最新版');
    });

  window.__wbStop = function () {
    clearInterval(window.__wbTimer);
    window.__wbLive = false;
    say('已停止, 本次共推送 ' + nSent + ' 字节');
  };
})();
