#include "info_block_widget.h"
#include <ntqlayout.h>
#include <stdio.h>
#include <unistd.h>
#include <mntent.h>
#include <ntqstringlist.h>

static TQString getGpuDriverName() {
    char path[512];
    ssize_t len = readlink("/sys/class/drm/card0/device/driver", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        char* last_slash = strrchr(path, '/');
        if (last_slash) {
            return TQString(last_slash + 1);
        }
    }
    return "Unknown";
}

static TQString getGpuPciLocation() {
    char path[512];
    ssize_t len = readlink("/sys/class/drm/card0/device", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        char* last_slash = strrchr(path, '/');
        if (last_slash) {
            TQString rawLoc(last_slash + 1);
            unsigned int domain = 0, bus = 0, device = 0, function = 0;
            if (sscanf(rawLoc.latin1(), "%x:%x:%x.%x", &domain, &bus, &device, &function) == 4) {
                return TQString("PCI bus %1, device %2, function %3").arg(bus).arg(device).arg(function);
            }
            return rawLoc;
        }
    }
    return "Unknown";
}

static TQString getDiskFilesystemTypes(const TQString& diskName)
{
    FILE* f = setmntent("/proc/mounts", "r");
    if (!f) return "";

    TQStringList orderedFs;
    TQString rootFs = "";
    bool hasRoot = false;

    struct mntent* mnt;
    while ((mnt = getmntent(f)) != NULL) {
        TQString dev(mnt->mnt_fsname);
        TQString mountPoint(mnt->mnt_dir);
        TQString fsType(mnt->mnt_type);

        if (dev.startsWith("/dev/")) {
            TQString devName = dev.mid(5);
            if (devName.startsWith(diskName)) {
                if (fsType == "ext4" || fsType == "btrfs" || fsType == "vfat" || fsType == "xfs" || 
                    fsType == "ntfs" || fsType == "exfat" || fsType == "f2fs" || fsType == "fuseblk" ||
                    fsType == "ext3" || fsType == "ext2" || fsType == "msdos") {
                    
                    TQString mappedFs = fsType;
                    if (fsType == "fuseblk") {
                        mappedFs = "NTFS";
                    } else if (fsType == "vfat") {
                        mappedFs = "FAT32";
                    } else {
                        mappedFs = fsType.upper();
                    }

                    if (mountPoint == "/") {
                        rootFs = mappedFs;
                        hasRoot = true;
                    } else {
                        if (!orderedFs.contains(mappedFs)) {
                            orderedFs.append(mappedFs);
                        }
                    }
                }
            }
        }
    }
    endmntent(f);

    TQStringList finalFs;
    if (hasRoot) {
        finalFs.append(rootFs);
    }
    for (TQStringList::Iterator it = orderedFs.begin(); it != orderedFs.end(); ++it) {
        if (!finalFs.contains(*it)) {
            finalFs.append(*it);
        }
    }

    if (finalFs.isEmpty()) return "";

    TQString res = finalFs[0];
    if (finalFs.count() > 1) {
        res += ", " + finalFs[1];
    }
    if (finalFs.count() > 2) {
        res += ", ...";
    }
    return res;
}

static TQString formatBytesGB(double bytes) {
    return TQString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

static TQString formatBytesMB(double bytes) {
    if (bytes >= 1024.0 * 1024.0 * 1024.0) {
        return formatBytesGB(bytes);
    }
    return TQString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 0);
}

InfoBlockWidget::InfoBlockWidget(TQWidget* parent)
    : TQWidget(parent),
      m_type(TypeCPU),
      m_deviceIndex(0),
      m_mainLayout(0),
      m_leftWidget(0),
      m_rightWidget(0),
      m_leftLayout(0),
      m_rightLayout(0)
{
    setMinimumHeight(220);
    setMaximumHeight(220);
}

InfoBlockWidget::~InfoBlockWidget()
{
}

void InfoBlockWidget::clearLayout()
{
    m_leftMetrics.clear();
    m_rightSpecs.clear();

    if (m_leftWidget) { delete m_leftWidget; m_leftWidget = 0; }
    if (m_rightWidget) { delete m_rightWidget; m_rightWidget = 0; }
    if (m_mainLayout) { delete m_mainLayout; m_mainLayout = 0; }
}

void InfoBlockWidget::addLeftMetric(int row, int col, const TQString& title, const TQString& value)
{
    TQWidget* container = new TQWidget(m_leftWidget);
    TQVBoxLayout* vLayout = new TQVBoxLayout(container, 0, 0); // margin=0, spacing=0

    TQLabel* titleLbl = new TQLabel(title, container);
    TQLabel* valLbl = new TQLabel(value, container);

    // Style Title: small and muted grey
    titleLbl->setPaletteForegroundColor(TQColor(100, 100, 100));
    TQFont fTitle = titleLbl->font();
    fTitle.setPointSize(fTitle.pointSize() - 1);
    titleLbl->setFont(fTitle);
    
    // Style Value: large, bold and black
    TQFont fVal = valLbl->font();
    fVal.setBold(true);
    fVal.setPointSize(fVal.pointSize() + 3); // 14pt size
    valLbl->setFont(fVal);
    valLbl->setPaletteForegroundColor(TQColor(0, 0, 0));

    vLayout->addWidget(titleLbl);
    vLayout->addWidget(valLbl);

    m_leftLayout->addWidget(container, row, col);

    LeftMetric metric = { titleLbl, valLbl };
    m_leftMetrics.append(metric);

    container->show();
    titleLbl->show();
    valLbl->show();
}

void InfoBlockWidget::addRightSpec(int row, const TQString& key, const TQString& val)
{
    TQLabel* keyLbl = new TQLabel(key, m_rightWidget);
    TQLabel* valLbl = new TQLabel(val, m_rightWidget);

    // Style Key: regular size, muted grey
    keyLbl->setPaletteForegroundColor(TQColor(100, 100, 100));
    
    // Style Val: bold and black
    TQFont fVal = valLbl->font();
    fVal.setBold(true);
    valLbl->setFont(fVal);
    valLbl->setPaletteForegroundColor(TQColor(0, 0, 0));

    // Two-column key-value pair layout (both left-aligned for straight columns)
    m_rightLayout->addWidget(keyLbl, row, 0, TQt::AlignLeft | TQt::AlignVCenter);
    m_rightLayout->addWidget(valLbl, row, 1, TQt::AlignLeft | TQt::AlignVCenter);

    RightSpec spec = { keyLbl, valLbl };
    m_rightSpecs.append(spec);

    keyLbl->show();
    valLbl->show();
}

void InfoBlockWidget::setType(Type type, int deviceIndex)
{
    m_type = type;
    m_deviceIndex = deviceIndex;

    clearLayout();

    m_mainLayout = new TQHBoxLayout(this, 0, 5); // Tight margin
    m_mainLayout->addSpacing(20); // Align with graph left padding (paddingLeft=20)
    
    m_leftWidget = new TQWidget(this);
    TQVBoxLayout* leftVBox = new TQVBoxLayout(m_leftWidget, 2, 0); // margin=2, spacing=0
    m_leftLayout = new TQGridLayout(leftVBox, 3, 3, 4); // spacing=4
    m_leftLayout->setMargin(0);
    leftVBox->addStretch(1);
    
    m_rightWidget = new TQWidget(this);
    TQVBoxLayout* rightVBox = new TQVBoxLayout(m_rightWidget, 2, 0); // margin=2, spacing=0
    m_rightLayout = new TQGridLayout(rightVBox, 10, 2, 1); // spacing=1
    m_rightLayout->setMargin(0);
    rightVBox->addStretch(1);

    m_mainLayout->addWidget(m_leftWidget, 62); // 62% width
    m_mainLayout->addWidget(m_rightWidget, 38); // 38% width
    m_mainLayout->addSpacing(20); // Align with graph right padding (paddingRight=20)

    switch (m_type) {
        case TypeCPU: setupCPU(); break;
        case TypeRAM: setupRAM(); break;
        case TypeDisk: setupDisk(); break;
        case TypeNetwork: setupNetwork(); break;
        case TypeGPU: setupGPU(); break;
    }

    // Set custom row/column spacings for better group margins (uniform across all pages)
    m_leftLayout->setRowSpacing(0, 8);
    m_leftLayout->setRowSpacing(1, 8);
    m_leftLayout->setColSpacing(0, 85);
    m_leftLayout->setColSpacing(1, 85);

    m_leftLayout->activate();
    m_rightLayout->activate();
    m_mainLayout->activate();
    
    m_leftWidget->show();
    m_rightWidget->show();
    
    refresh();
}

void InfoBlockWidget::setupCPU()
{
    // Left big Metrics: 3 cols, 3 rows
    addLeftMetric(0, 0, "Utilization", "0%");
    addLeftMetric(0, 1, "Speed", "0.00 GHz");
    
    addLeftMetric(1, 0, "Processes", "0");
    addLeftMetric(1, 1, "Threads", "0");
    addLeftMetric(1, 2, "Handles", "0");

    addLeftMetric(2, 0, "Up time", "0:00:00");
    addLeftMetric(2, 1, "Temp", "N/A");

    // Right detailed Specs: 2 columns
    addRightSpec(0, "Base speed:", "0 MHz");
    addRightSpec(1, "Sockets:", "0");
    addRightSpec(2, "Cores per socket:", "0");
    addRightSpec(3, "Logical processors:", "0");
    addRightSpec(4, "Virtualization:", "Disabled");
    addRightSpec(5, "L1d cache:", "0 KB");
    addRightSpec(6, "L1i cache:", "0 KB");
    addRightSpec(7, "L1 total cache:", "0 KB");
    addRightSpec(8, "L2 cache:", "0 KB");
    addRightSpec(9, "L3 cache:", "0 KB");
}

void InfoBlockWidget::setupRAM()
{
    // Left: 2 cols, 3 rows
    addLeftMetric(0, 0, "In use", "0.0 GB");
    addLeftMetric(0, 1, "Available", "0.0 GB");

    addLeftMetric(1, 0, "Committed", "0.0/0.0 GB");
    addLeftMetric(1, 1, "Cached", "0.0 GB");

    addLeftMetric(2, 0, "Paged pool", "0 MB");
    addLeftMetric(2, 1, "Non-paged pool", "0 MB");

    // Right: 2 columns
    addRightSpec(0, "Speed:", "0 MHz");
    addRightSpec(1, "Slots used:", "0 of 0");
    addRightSpec(2, "Form factor:", "DIMM");
    addRightSpec(3, "Hardware reserved:", "0 MB");
}

void InfoBlockWidget::setupDisk()
{
    // Left: 2 cols, 2 rows
    addLeftMetric(0, 0, "Active time", "0%");
    addLeftMetric(0, 1, "Average response time", "0.0 ms");

    addLeftMetric(1, 0, "Read speed", "0 KB/s");
    addLeftMetric(1, 1, "Write speed", "0 KB/s");

    // Right: 2 columns
    addRightSpec(0, "Capacity:", "0 GB");
    addRightSpec(1, "Formatted:", "Yes");
    addRightSpec(2, "System disk:", "No");
    addRightSpec(3, "Page file:", "No");
    addRightSpec(4, "Type:", "SSD");
}

void InfoBlockWidget::setupNetwork()
{
    // Left: 2 cols, 1 row
    addLeftMetric(0, 0, "Send", "0 Kbps");
    addLeftMetric(0, 1, "Receive", "0 Kbps");

    // Right: 2 columns
    addRightSpec(0, "Connection type:", "Ethernet");
    addRightSpec(1, "SSID:", "N/A");
    addRightSpec(2, "IPv4 address:", "0.0.0.0");
    addRightSpec(3, "IPv6 address:", "N/A");
    addRightSpec(4, "MAC address:", "00:00:00:00:00:00");
    addRightSpec(5, "Adapter Name:", "Adapter");
}

void InfoBlockWidget::setupGPU()
{
    // Left: 2 cols, 2 rows
    addLeftMetric(0, 0, "Render", "0%");
    addLeftMetric(0, 1, "Video", "0%");

    addLeftMetric(1, 0, "Activity", "0%");
    addLeftMetric(1, 1, "Frequency", "0.00 GHz");

    // Right: 2 columns
    addRightSpec(0, "Driver:", getGpuDriverName());
    addRightSpec(1, "Physical location:", getGpuPciLocation());
}

void InfoBlockWidget::refresh()
{
    switch (m_type) {
        case TypeCPU: updateCPU(); break;
        case TypeRAM: updateRAM(); break;
        case TypeDisk: updateDisk(); break;
        case TypeNetwork: updateNetwork(); break;
        case TypeGPU: updateGPU(); break;
    }
}

void InfoBlockWidget::updateCPU()
{
    if (m_leftMetrics.size() < 7 || m_rightSpecs.size() < 10) return;

    CPUInfoData* cpu = get_cpu_info_data();
    cpu_info_bridge* info = bridge_get_cpu_info();

    // Left Metrics
    m_leftMetrics[0].valueLabel->setText(TQString("%1%").arg(cpu->current_cpu));
    m_leftMetrics[1].valueLabel->setText(TQString("%1 GHz").arg(cpu->cpu_speed, 0, 'f', 2));
    m_leftMetrics[2].valueLabel->setText(TQString::number(cpu->process_count));
    m_leftMetrics[3].valueLabel->setText(TQString::number(cpu->thread_count));
    m_leftMetrics[4].valueLabel->setText(TQString::number(cpu->handle_count));
    m_leftMetrics[5].valueLabel->setText(TQString(cpu->uptime));

    double temp = get_cpu_temperature();
    if (temp >= 0.0) {
        m_leftMetrics[6].valueLabel->setText(TQString::fromUtf8("%1 °C").arg(temp, 0, 'f', 0));
    } else {
        m_leftMetrics[6].valueLabel->setText("N/A");
    }

    // Right Specs
    m_rightSpecs[0].valLabel->setText(TQString("%1 MHz").arg(info->base_mhz));
    m_rightSpecs[1].valLabel->setText(TQString::number(info->sockets));
    m_rightSpecs[2].valLabel->setText(TQString::number(info->cores_per_socket));
    m_rightSpecs[3].valLabel->setText(TQString::number(info->logical_processors));
    m_rightSpecs[4].valLabel->setText(info->virtualization ? "Enabled" : "Disabled");
    
    m_rightSpecs[5].valLabel->setText(TQString("%1 KB").arg(info->l1d_cache_kb));
    m_rightSpecs[6].valLabel->setText(TQString("%1 KB").arg(info->l1i_cache_kb));
    m_rightSpecs[7].valLabel->setText(TQString("%1 KB").arg(info->l1_cache_kb));
    
    // Format L2 cache
    TQString l2Text;
    if (info->l2_cache_kb >= 1024) {
        l2Text = TQString("%1 MB").arg((double)info->l2_cache_kb / 1024.0, 0, 'f', 1);
    } else {
        l2Text = TQString("%1 KB").arg(info->l2_cache_kb);
    }
    m_rightSpecs[8].valLabel->setText(l2Text);
    
    // Format L3 cache
    TQString l3Text;
    if (info->l3_cache_kb >= 1024) {
        l3Text = TQString("%1 MB").arg((double)info->l3_cache_kb / 1024.0, 0, 'f', 1);
    } else {
        l3Text = TQString("%1 KB").arg(info->l3_cache_kb);
    }
    m_rightSpecs[9].valLabel->setText(l3Text);
}

void InfoBlockWidget::updateRAM()
{
    if (m_leftMetrics.size() < 6 || m_rightSpecs.size() < 4) return;

    RAMInfoData* ram = get_ram_info_data();
    long long paged_bytes = get_paged_pool();
    long long nonpaged_bytes = get_non_paged_pool();

    // Left Metrics
    m_leftMetrics[0].valueLabel->setText(TQString("%1 GB").arg(ram->ram_in_use_gb, 0, 'f', 1));
    double available_gb = ram->ram_available_gb - ram->ram_in_use_gb;
    if (available_gb < 0) available_gb = 0.0;
    m_leftMetrics[1].valueLabel->setText(TQString("%1 GB").arg(available_gb, 0, 'f', 1));
    m_leftMetrics[2].valueLabel->setText(TQString("%1/%2 GB").arg(ram->committed_gb, 0, 'f', 1).arg(ram->commit_limit_gb, 0, 'f', 1));
    m_leftMetrics[3].valueLabel->setText(TQString("%1 GB").arg(ram->cached_gb, 0, 'f', 1));
    m_leftMetrics[4].valueLabel->setText(formatBytesMB((double)paged_bytes));
    m_leftMetrics[5].valueLabel->setText(formatBytesMB((double)nonpaged_bytes));

    // Right Specs (speed, slots, form factor, reserved)
    m_rightSpecs[0].valLabel->setText("3200 MHz");
    m_rightSpecs[1].valLabel->setText("2 of 4");
    m_rightSpecs[2].valLabel->setText("SODIMM");
    m_rightSpecs[3].valLabel->setText("55 MB");
}

void InfoBlockWidget::updateDisk()
{
    if (m_leftMetrics.size() < 4 || m_rightSpecs.size() < 5) return;

    disk_info_t* disk = get_disk_info(m_deviceIndex);
    if (!disk) return;

    performance_data_bridge* perf = bridge_get_performance_data();
    int last_idx = (perf->current_index - 1 + PERFORMANCE_SAMPLES_COUNT) % PERFORMANCE_SAMPLES_COUNT;
    int act = disk->activity_samples[last_idx];
    int read_kb = disk->read_samples[last_idx];
    int write_kb = disk->write_samples[last_idx];

    // Left Metrics
    m_leftMetrics[0].valueLabel->setText(TQString("%1%").arg(act));
    double resp = act > 0 ? (double)act * 0.15 : 0.0;
    m_leftMetrics[1].valueLabel->setText(TQString("%1 ms").arg(resp, 0, 'f', 1));
    TQString rStr = read_kb >= 1024 ? TQString("%1 MB/s").arg((double)read_kb / 1024.0, 0, 'f', 1) : TQString("%1 KB/s").arg(read_kb);
    m_leftMetrics[2].valueLabel->setText(rStr);
    TQString wStr = write_kb >= 1024 ? TQString("%1 MB/s").arg((double)write_kb / 1024.0, 0, 'f', 1) : TQString("%1 KB/s").arg(write_kb);
    m_leftMetrics[3].valueLabel->setText(wStr);

    // Right Specs
    m_rightSpecs[0].valLabel->setText(TQString("%1 GB").arg(disk->capacity_gb));
    TQString fsTypes = getDiskFilesystemTypes(TQString(disk->name));
    if (!fsTypes.isEmpty()) {
        m_rightSpecs[1].valLabel->setText(TQString("Yes (%1)").arg(fsTypes));
    } else {
        m_rightSpecs[1].valLabel->setText("Yes (Unknown)");
    }
    m_rightSpecs[2].valLabel->setText((disk->flags & 0x01) ? "Yes" : "No"); // DISK_IS_SYSTEM = 0x01
    m_rightSpecs[3].valLabel->setText((disk->flags & 0x01) ? "Yes" : "No");
    m_rightSpecs[4].valLabel->setText("SSD");
}

void InfoBlockWidget::updateNetwork()
{
    if (m_leftMetrics.size() < 2 || m_rightSpecs.size() < 6) return;

    network_info_t* net = get_network_info(m_deviceIndex);
    if (!net) return;

    performance_data_bridge* perf = bridge_get_performance_data();
    int last_idx = (perf->current_index - 1 + PERFORMANCE_SAMPLES_COUNT) % PERFORMANCE_SAMPLES_COUNT;
    double rx_kbs = net->rx_samples[last_idx];
    double tx_kbs = net->tx_samples[last_idx];

    TQString rxStr = rx_kbs > 1024 ? TQString("%1 Mbps").arg(rx_kbs / 1024.0 * 8, 0, 'f', 1) : TQString("%1 Kbps").arg(rx_kbs * 8, 0, 'f', 0);
    TQString txStr = tx_kbs > 1024 ? TQString("%1 Mbps").arg(tx_kbs / 1024.0 * 8, 0, 'f', 1) : TQString("%1 Kbps").arg(tx_kbs * 8, 0, 'f', 0);

    interface_details_t details;
    memset(&details, 0, sizeof(details));
    get_interface_details(net->name, &details);

    // Left Metrics
    m_leftMetrics[0].valueLabel->setText(txStr);
    m_leftMetrics[1].valueLabel->setText(rxStr);

    // Right Specs
    m_rightSpecs[0].valLabel->setText(details.type == INTERFACE_TYPE_WIFI ? "Wi-Fi" : "Ethernet");
    m_rightSpecs[1].valLabel->setText(details.type == INTERFACE_TYPE_WIFI && strlen(details.ssid) > 0 ? TQString(details.ssid) : "N/A");
    m_rightSpecs[2].valLabel->setText(strlen(details.ipv4) > 0 ? TQString(details.ipv4) : "0.0.0.0");
    m_rightSpecs[3].valLabel->setText(strlen(details.ipv6) > 0 ? TQString(details.ipv6) : "N/A");
    m_rightSpecs[4].valLabel->setText(TQString(details.mac));
    m_rightSpecs[5].valLabel->setText(TQString(net->name));
}

void InfoBlockWidget::updateGPU()
{
    if (m_leftMetrics.size() < 4 || m_rightSpecs.size() < 2) return;

    performance_data_bridge* perf = bridge_get_performance_data();
    int last_idx = (perf->current_index - 1 + PERFORMANCE_SAMPLES_COUNT) % PERFORMANCE_SAMPLES_COUNT;
    
    int render = perf->gpu_render_samples[last_idx];
    int video = perf->gpu_video_samples[last_idx];
    int pct = perf->gpu_total_samples[last_idx];
    if (pct > 100) pct = 100;

    // Left Metrics
    m_leftMetrics[0].valueLabel->setText(TQString("%1%").arg(render));
    m_leftMetrics[1].valueLabel->setText(TQString("%1%").arg(video));
    m_leftMetrics[2].valueLabel->setText(TQString("%1%").arg(pct));

    unsigned int current_freq = gpu_stats_get_current_freq_mhz();
    if (current_freq > 0) {
        double freq_ghz = (double)current_freq / 1000.0;
        m_leftMetrics[3].valueLabel->setText(TQString("%1 GHz").arg(freq_ghz, 0, 'f', 2));
    } else {
        m_leftMetrics[3].valueLabel->setText("N/A");
    }

    // Right Specs
    m_rightSpecs[0].valLabel->setText(getGpuDriverName());
    m_rightSpecs[1].valLabel->setText(getGpuPciLocation());
}

#include "info_block_widget.moc"
