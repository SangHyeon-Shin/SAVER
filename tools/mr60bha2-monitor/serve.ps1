param(
    [int]$Port = 8766,
    [switch]$NoBrowser
)

$ErrorActionPreference = 'Stop'
$monitorRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$indexPath = Join-Path $monitorRoot 'index.html'
$url = "http://localhost:$Port/"

if (-not (Test-Path -LiteralPath $indexPath)) {
    throw "index.html을 찾을 수 없습니다: $indexPath"
}

$listener = [System.Net.HttpListener]::new()
$listener.Prefixes.Add($url)
$listener.Start()

Write-Host "MR60BHA2 모니터: $url"
Write-Host '종료하려면 이 창에서 Ctrl+C를 누르세요.'

if (-not $NoBrowser) {
    Start-Process $url
}

try {
    while ($listener.IsListening) {
        $context = $listener.GetContext()
        try {
            if ($context.Request.Url.AbsolutePath -eq '/') {
                $content = [System.IO.File]::ReadAllBytes($indexPath)
                $context.Response.StatusCode = 200
                $context.Response.ContentType = 'text/html; charset=utf-8'
                $context.Response.ContentLength64 = $content.Length
                $context.Response.OutputStream.Write($content, 0, $content.Length)
            } else {
                $context.Response.StatusCode = 404
            }
        } finally {
            $context.Response.OutputStream.Close()
        }
    }
} finally {
    $listener.Stop()
    $listener.Close()
}
