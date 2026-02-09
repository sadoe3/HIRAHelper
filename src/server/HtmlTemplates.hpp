#pragma once
#include <string>

// Raw String Literal (R"()")을 사용하여 HTML 내장
const std::string CONFIG_HTML = R"(
<!DOCTYPE html>
<html><head><title>HIRA Helper Config</title>
<meta charset="UTF-8">
<style>
body{font-family:'Segoe UI', sans-serif;max-width:600px;margin:40px auto;padding:20px;border:1px solid #ddd;box-shadow:0 4px 10px rgba(0,0,0,0.1);}
h2{color:#0078D4;text-align:center;}
label{display:block;margin:10px 0 5px;font-weight:bold;}
input{width:100%;padding:8px;box-sizing:border-box;margin-bottom:15px;border:1px solid #ccc;}
button{background:#0078D4;color:white;padding:12px;border:none;width:100%;cursor:pointer;font-size:16px;}
button:hover{background:#005a9e;}
.footer{margin-top:20px;text-align:center;font-size:12px;color:#888;}
</style>
</head><body>
<h2>⚙️ HIRA Helper Settings</h2>
<form action="/config" method="POST">
    <label>NAS IP (Short Term)</label>
    <input name="nas_short_ip" value="{{NAS_SHORT}}" placeholder="e.g. 127.0.0.1">
    
    <label>NAS IP (Long Term)</label>
    <input name="nas_long_ip" value="{{NAS_LONG}}" placeholder="e.g. 192.168.1.12">

    <label>Service Port (Restart Required)</label>
    <input type="number" name="port" value="{{PORT}}">

    <label>Local Cache Root Path</label>
    <input name="cache_root" value="{{CACHE_ROOT}}">

    <button type="submit">Save Configuration</button>
</form>
<div class="footer">HIRA Helper Solution v2.0</div>
</body></html>
)";