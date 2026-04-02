/**
 * @file HtmlTemplates.hpp
 * @brief config 설정을 위한 html 코드.
 */

#pragma once
#include <string>

const std::string CONFIG_HTML = R"(
<!DOCTYPE html>
<html><head><title>HIRA Helper Config</title>
<meta charset="UTF-8">
<style>
body{font-family:'Segoe UI', sans-serif;max-width:600px;margin:40px auto;padding:20px;border:1px solid #ddd;box-shadow:0 4px 10px rgba(0,0,0,0.1);}
h2{color:#0078D4;text-align:center;border-bottom: 2px solid #eee;padding-bottom: 10px;}
.section {background: #f9f9f9; padding: 15px; border-radius: 5px; margin-bottom: 15px;}
label{display:block;margin:10px 0 5px;font-weight:bold;font-size: 14px;}
input[type=text], input[type=number]{width:100%;padding:8px;box-sizing:border-box;margin-bottom:10px;border:1px solid #ccc;border-radius:4px;}
input[type=checkbox] {transform: scale(1.5); margin-right: 10px;}
button{background:#0078D4;color:white;padding:12px;border:none;width:100%;cursor:pointer;font-size:16px;border-radius:4px;margin-top:10px;}
button:hover{background:#005a9e;}
.footer{margin-top:20px;text-align:center;font-size:12px;color:#888;}
</style>
</head><body>
<h2>⚙️ HIRA Helper Settings</h2>
<form action="/config" method="POST">
    
    <div class="section">
        <label>NAS IP (Short Term)</label>
        <input type="text" name="nas_short_ip" value="{{NAS_SHORT}}">
        
        <label>NAS IP (Long Term)</label>
        <input type="text" name="nas_long_ip" value="{{NAS_LONG}}">

        <label>Service Port (Restart Required)</label>
        <input type="number" name="port" value="{{PORT}}">

        <label>Local Cache Root Path</label>
        <input type="text" name="cache_root" value="{{CACHE_ROOT}}">
    </div>

    <div class="section">
        <label>Disk Management (Retention)</label>
        <label style="font-weight:normal;">
            <input type="checkbox" name="cleaner_enabled" {{CLEANER_CHECKED}}> Enable Auto Cleanup
        </label>

        <label>Cleaner Interval (Days)</label>
        <input type="number" name="cleaner_interval_days" value="{{INTERVAL}}">

        <label>File Retention (Days)</label>
        <input type="number" name="retention_days" value="{{RETENTION}}">
    </div>

    <button type="submit">Save Configuration</button>
</form>
<div class="footer">HIRA Helper Solution v3.1</div>
</body></html>
)";