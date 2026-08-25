#!/bin/bash
set -e

# ==============================================================================
# Script de mise à jour du dépôt APT GitHub Pages pour Task Manager (taskmgr)
# ==============================================================================

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
PAGES_DIR="/tmp/taskmgr_gh_pages_$$"
REMOTE_URL=$(git config --get remote.origin.url)

echo "=================================================="
echo " Task Manager - APT Repository Builder (gh-pages)"
echo " Repository URL : $REMOTE_URL"
echo "=================================================="

# Check prerequisites
for cmd in dpkg-scanpackages apt-ftparchive git gzip; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Error: Required command '$cmd' is not installed."
        exit 1
    fi
done

# Find deb packages at repo root
shopt -s nullglob
DEB_FILES=($(ls -t "$REPO_DIR"/taskmgr_*.deb 2>/dev/null || true))
shopt -u nullglob

if [ ${#DEB_FILES[@]} -eq 0 ]; then
    echo "Error: No 'taskmgr_*.deb' package found at repo root. Please run ./build_deb.sh first."
    exit 1
fi

# Clone or initialize gh-pages branch
rm -rf "$PAGES_DIR"
mkdir -p "$PAGES_DIR"

REMOTE_PAGES=$(git ls-remote --heads "$REMOTE_URL" gh-pages 2>/dev/null || true)
if [ -n "$REMOTE_PAGES" ]; then
    echo "Cloning existing gh-pages branch from remote..."
    git clone --single-branch --branch gh-pages "$REMOTE_URL" "$PAGES_DIR"
else
    echo "Initializing new gh-pages branch..."
    cd "$PAGES_DIR"
    git init
    git checkout -b gh-pages
    git remote add origin "$REMOTE_URL"
fi

cd "$PAGES_DIR"

# Create standard APT pool and dists structure
POOL_DIR="$PAGES_DIR/pool/main/t/taskmgr"
DISTS_DIR="$PAGES_DIR/dists/stable/main/binary-amd64"
mkdir -p "$POOL_DIR"
mkdir -p "$DISTS_DIR"

# Copy all .deb packages from root
for deb in "${DEB_FILES[@]}"; do
    cp -u "$deb" "$POOL_DIR/" 2>/dev/null || cp -a "$deb" "$POOL_DIR/"
    echo "  -> Added DEB: $(basename "$deb")"
done

# Copy latest .qsi if present
QSI_FILES=($(ls -t "$REPO_DIR"/setup_taskmgr_*.qsi 2>/dev/null || true))
if [ ${#QSI_FILES[@]} -gt 0 ]; then
    for qsi in "${QSI_FILES[@]}"; do
        cp -u "$qsi" "$PAGES_DIR/" 2>/dev/null || cp -a "$qsi" "$PAGES_DIR/"
        echo "  -> Added QSI: $(basename "$qsi")"
    done
fi

# Copy latest .AppImage if present
APPIMAGE_FILES=($(ls -t "$REPO_DIR"/taskmgr-*.AppImage "$REPO_DIR"/taskmgr_*.AppImage 2>/dev/null || true))
if [ ${#APPIMAGE_FILES[@]} -gt 0 ]; then
    for appimage in "${APPIMAGE_FILES[@]}"; do
        cp -u "$appimage" "$PAGES_DIR/" 2>/dev/null || cp -a "$appimage" "$PAGES_DIR/"
        echo "  -> Added AppImage: $(basename "$appimage")"
    done
fi

# Generate Packages & Packages.gz
echo "Generating Packages index..."
dpkg-scanpackages --multiversion pool/ /dev/null > "$DISTS_DIR/Packages"
gzip -9c "$DISTS_DIR/Packages" > "$DISTS_DIR/Packages.gz"

# Generate Release file
echo "Generating Release manifest..."
apt-ftparchive \
  -o APT::FTPArchive::Release::Origin="Taskmgr" \
  -o APT::FTPArchive::Release::Label="Task Manager APT Repository" \
  -o APT::FTPArchive::Release::Suite="stable" \
  -o APT::FTPArchive::Release::Codename="stable" \
  -o APT::FTPArchive::Release::Architectures="amd64" \
  -o APT::FTPArchive::Release::Components="main" \
  -o APT::FTPArchive::Release::Description="APT Repository for Task Manager (Trinity Desktop & Linux)" \
  release "$PAGES_DIR/dists/stable" > "$PAGES_DIR/dists/stable/Release"

# Copy assets (about image, favicon, etc.)
if [ -f "$REPO_DIR/about_taskmgr.png" ]; then
    cp -a "$REPO_DIR/about_taskmgr.png" "$PAGES_DIR/"
fi
if [ -f "$REPO_DIR/favicon.png" ]; then
    cp -a "$REPO_DIR/favicon.png" "$PAGES_DIR/"
fi

# Create .nojekyll to prevent GitHub Pages Jekyll processing
touch "$PAGES_DIR/.nojekyll"

# Find latest file names for HTML download buttons
LATEST_DEB_NAME=$(basename "${DEB_FILES[0]}")
LATEST_QSI_NAME=""
if [ ${#QSI_FILES[@]} -gt 0 ]; then
    LATEST_QSI_NAME=$(basename "${QSI_FILES[0]}")
fi
LATEST_APPIMAGE_NAME=""
if [ ${#APPIMAGE_FILES[@]} -gt 0 ]; then
    LATEST_APPIMAGE_NAME=$(basename "${APPIMAGE_FILES[0]}")
fi

cat << EOF > "$PAGES_DIR/index.html"
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Task Manager - APT Repository & Downloads</title>
  <link rel="icon" type="image/png" href="favicon.png">
  <style>
    :root {
      --bg: #0f172a;
      --card-bg: #1e293b;
      --accent: #38bdf8;
      --accent-grad: linear-gradient(135deg, #0284c7, #38bdf8);
      --text: #f1f5f9;
      --text-muted: #94a3b8;
      --code-bg: #0b1120;
      --border: #334155;
      --highlight: #22c55e;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      background-color: var(--bg);
      color: var(--text);
      line-height: 1.6;
      padding: 40px 20px;
    }
    .container {
      max-width: 860px;
      margin: 0 auto;
    }
    header {
      text-align: center;
      margin-bottom: 40px;
    }
    .logo {
      width: 110px;
      height: 110px;
      margin-bottom: 18px;
      filter: drop-shadow(0 8px 24px rgba(56, 189, 248, 0.35));
      border-radius: 18px;
      transition: transform 0.3s cubic-bezier(0.34, 1.56, 0.64, 1);
    }
    .logo:hover {
      transform: scale(1.06) rotate(2deg);
    }
    .badge-group {
      display: flex;
      justify-content: center;
      gap: 10px;
      margin-bottom: 14px;
      flex-wrap: wrap;
    }
    .badge {
      display: inline-block;
      padding: 5px 14px;
      font-size: 0.82rem;
      font-weight: 600;
      color: #fff;
      background: var(--accent-grad);
      border-radius: 20px;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }
    .badge-green {
      background: linear-gradient(135deg, #15803d, #22c55e);
    }
    .badge-purple {
      background: linear-gradient(135deg, #6366f1, #a855f7);
    }
    h1 {
      font-size: 2.3rem;
      font-weight: 800;
      margin-bottom: 10px;
      letter-spacing: -0.5px;
    }
    p.lead {
      font-size: 1.15rem;
      color: var(--text-muted);
      max-width: 680px;
      margin: 0 auto;
    }
    .card {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 14px;
      padding: 26px;
      margin-bottom: 24px;
      box-shadow: 0 10px 30px rgba(0, 0, 0, 0.3);
    }
    h2 {
      font-size: 1.35rem;
      margin-bottom: 16px;
      display: flex;
      align-items: center;
      gap: 10px;
      color: #fff;
    }
    pre {
      background: var(--code-bg);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 16px 20px;
      overflow-x: auto;
      font-family: "JetBrains Mono", "Fira Code", Consolas, "Courier New", monospace;
      font-size: 0.92rem;
      color: #38bdf8;
      margin-bottom: 14px;
    }
    .features-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
      gap: 16px;
      margin-top: 14px;
    }
    .feature-item {
      background: var(--code-bg);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 14px 18px;
    }
    .feature-item h3 {
      font-size: 1rem;
      color: #38bdf8;
      margin-bottom: 4px;
    }
    .feature-item p {
      font-size: 0.88rem;
      color: var(--text-muted);
    }
    .btn-group {
      display: flex;
      flex-wrap: wrap;
      gap: 14px;
      margin-top: 18px;
    }
    .btn {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      padding: 12px 22px;
      border-radius: 10px;
      text-decoration: none;
      font-weight: 600;
      font-size: 0.95rem;
      transition: all 0.2s ease;
    }
    .btn-primary {
      background: var(--accent-grad);
      color: #fff;
      box-shadow: 0 4px 14px rgba(2, 132, 199, 0.35);
    }
    .btn-primary:hover {
      opacity: 0.92;
      transform: translateY(-2px);
      box-shadow: 0 6px 20px rgba(2, 132, 199, 0.45);
    }
    .btn-secondary {
      background: var(--code-bg);
      color: var(--text);
      border: 1px solid var(--border);
    }
    .btn-secondary:hover {
      background: #1e293b;
      border-color: #475569;
      transform: translateY(-2px);
    }
    footer {
      text-align: center;
      margin-top: 45px;
      font-size: 0.9rem;
      color: var(--text-muted);
    }
    footer a { color: var(--accent); text-decoration: none; }
    footer a:hover { text-decoration: underline; }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <img src="about_taskmgr.png" alt="Task Manager Logo" class="logo">
      <br>
      <div class="badge-group">
        <div class="badge">Official APT Repository</div>
        <div class="badge badge-green">TDE &amp; Linux Native</div>
        <div class="badge badge-purple">x86_64</div>
      </div>
      <h1>Task Manager</h1>
      <p class="lead">Lightweight, modern task and system resource monitor for Trinity Desktop Environment (TDE) &amp; Linux systems.</p>
    </header>

    <div class="card">
      <h2>🚀 Method 1: Add the APT Repository (Recommended)</h2>
      <p style="margin-bottom: 14px; color: var(--text-muted);">
        Add the official repository to your system to receive regular automated updates via <code>apt</code>:
      </p>
      <pre><code>echo "deb [trusted=yes] https://seb3773.github.io/taskmgr/ stable main" | sudo tee /etc/apt/sources.list.d/taskmgr.list
sudo apt update
sudo apt install taskmgr</code></pre>
      <p style="font-size: 0.88rem; color: var(--text-muted);">
        Compatible with Debian, Q4OS, Devuan, Ubuntu, Linux Mint and derivatives.
      </p>
    </div>

    <div class="card">
      <h2>📦 Method 2: Standalone &amp; Direct Downloads</h2>
      <p style="color: var(--text-muted); margin-bottom: 12px;">
        Choose the format best suited for your setup:
      </p>
      <div class="btn-group">
        ${LATEST_QSI_NAME:+<a class="btn btn-primary" href="${LATEST_QSI_NAME}">📥 Download Q4OS Installer (.qsi)</a>}
        <a class="btn btn-secondary" href="pool/main/t/taskmgr/${LATEST_DEB_NAME}">📦 Download Debian Package (.deb)</a>
        ${LATEST_APPIMAGE_NAME:+<a class="btn btn-secondary" href="${LATEST_APPIMAGE_NAME}">🐧 Download AppImage (Portable)</a>}
      </div>
      <p style="font-size: 0.85rem; color: #64748b; margin-top: 14px;">
        * Note: The Q4OS installer (.qsi) automatically configures the APT repository during installation for future updates.
      </p>
    </div>

    <div class="card">
      <h2>✨ Key Features</h2>
      <div class="features-grid">
        <div class="feature-item">
          <h3>⚡ Process Tree &amp; Control</h3>
          <p>Tree hierarchy, signals, clean termination, priority adjustment, and detailed memory analysis (PSS, RSS, VSZ).</p>
        </div>
        <div class="feature-item">
          <h3>📊 Real-Time Graphs</h3>
          <p>Smooth real-time graphs for CPU (overall &amp; per-core), RAM, Swap, Disk throughput, Network, and GPU engines.</p>
        </div>
        <div class="feature-item">
          <h3>🚀 Startup Manager</h3>
          <p>Manage autostart applications for both standard XDG entries and native Trinity Desktop (TDE) conditions.</p>
        </div>
        <div class="feature-item">
          <h3>⚙️ Services Management</h3>
          <p>Inspect, start, stop, restart, enable, or disable systemd system services directly with root elevation support.</p>
        </div>
      </div>
    </div>

    <footer>
      <p>Source Code &amp; Issue Tracker: <a href="https://github.com/seb3773/taskmgr" target="_blank">github.com/seb3773/taskmgr</a></p>
      <p style="margin-top: 6px;">Developed with ❤️ for the Trinity Desktop Environment (TDE) &amp; Linux community.</p>
    </footer>
  </div>
</body>
</html>
EOF

# Git commit and push to gh-pages
echo "Committing and pushing to gh-pages branch..."
git add -A
git commit -m "Update APT repository and packages: $(date +'%Y-%m-%d %H:%M:%S')" || echo "No changes to commit."
git push origin gh-pages

echo "Cleaning up temporary directory..."
rm -rf "$PAGES_DIR"

echo "=================================================="
echo " SUCCESS: APT repository updated on gh-pages!"
echo " URL: https://seb3773.github.io/taskmgr/"
echo "=================================================="
