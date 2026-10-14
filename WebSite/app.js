// c_web 网站前端脚本：调用本服务器的动态路由并展示原始响应
(function () {
  "use strict";

  var out = document.getElementById("out");

  function show(title, text) {
    out.textContent = "== " + title + " ==\n" + text;
  }

  // 探测一个路由：method 默认 GET，可带 body
  function probe(path, method, body) {
    var opt = { method: method || "GET", cache: "no-store" };
    if (body !== undefined) opt.body = body;

    show(method + " " + path + " …", "请求中…");
    fetch(path, opt).then(function (res) {
      return res.text().then(function (text) {
        var head = "HTTP " + res.status + " " + res.statusText +
                   "\nContent-Type: " + (res.headers.get("content-type") || "-") +
                   "\nContent-Length: " + (res.headers.get("content-length") || "-") +
                   "\n\n";
        show(method + " " + path, head + text);
      });
    }).catch(function (err) {
      show(method + " " + path, "请求失败：" + err);
    });
  }

  // --- 在线自测按钮 ---
  var buttons = document.querySelectorAll("button[data-probe]");
  for (var i = 0; i < buttons.length; i++) {
    (function (btn) {
      btn.addEventListener("click", function () {
        var path = btn.getAttribute("data-probe");
        if (btn.hasAttribute("data-sum")) {
          // 256KB 响应只要长度，避免刷屏
          fetch(path, { cache: "no-store" }).then(function (res) {
            return res.text().then(function (t) {
              show("GET " + path, "HTTP " + res.status +
                "\nContent-Type: " + (res.headers.get("content-type") || "-") +
                "\n声明长度: " + res.headers.get("content-length") +
                "\n实际收到: " + t.length + " 字节" +
                "\n首行内容: " + t.slice(0, 60) + "…");
            });
          });
        } else {
          probe(path, "GET");
        }
      });
    })(buttons[i]);
  }

  // --- /echo 查询串回显 ---
  var echoBtn = document.getElementById("echo-btn");
  if (echoBtn) {
    echoBtn.addEventListener("click", function () {
      var name = encodeURIComponent(document.getElementById("echo-name").value);
      var msg = encodeURIComponent(document.getElementById("echo-msg").value);
      probe("/echo?name=" + name + "&msg=" + msg, "GET");
    });
  }

  // --- POST /api/echo 表单回显 ---
  var post = document.createElement("button");
  post.textContent = "POST /api/echo";
  post.addEventListener("click", function () {
    probe("/api/echo", "POST", "ping-from-browser");
  });
  if (echoBtn && echoBtn.parentNode) echoBtn.parentNode.appendChild(post);

  // --- 首页状态徽标：用 /api/info 判断服务是否正常 ---
  var status = document.getElementById("status");
  function refreshStatus() {
    fetch("/api/info", { cache: "no-store" }).then(function (r) { return r.json(); })
      .then(function (info) {
        status.textContent = "运行中 · pid " + info.pid + " · 已运行 " + info.uptime + "s";
      })
      .catch(function () { status.textContent = "服务异常"; });
  }
  refreshStatus();

  // --- 页脚时钟 ---
  var clock = document.getElementById("clock");
  function tick() {
    if (clock) clock.textContent = new Date().toLocaleString("zh-CN");
  }
  tick();
  setInterval(tick, 1000);
  setInterval(refreshStatus, 30000);
})();
