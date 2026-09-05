#include <borealis.hpp>

#include <cstdlib>
#include <string>

namespace
{

constexpr const char* POC_MARKER = "PLAYWISE BOREALIS POC";

enum class Page
{
    CHILD,
    PARENT,
};

NVGcolor themeColor(const std::string& name)
{
    return brls::Application::getTheme()[name];
}

brls::Label* label(const std::string& text, float size, const std::string& color, float height = brls::View::AUTO)
{
    auto* view = new brls::Label();
    view->setText(text);
    view->setFontSize(size);
    view->setTextColor(themeColor(color));
    view->setHorizontalAlign(brls::HorizontalAlign::LEFT);
    view->setVerticalAlign(brls::VerticalAlign::CENTER);
    view->setSingleLine(false);
    view->setWidthPercentage(100);
    view->setHeight(height);
    return view;
}

brls::Box* row(float height = brls::View::AUTO)
{
    auto* view = new brls::Box(brls::Axis::ROW);
    view->setWidthPercentage(100);
    view->setHeight(height);
    view->setAlignItems(brls::AlignItems::CENTER);
    return view;
}

brls::Box* column(float width = brls::View::AUTO)
{
    auto* view = new brls::Box(brls::Axis::COLUMN);
    view->setWidth(width);
    view->setHeight(brls::View::AUTO);
    return view;
}

brls::Box* card(float height)
{
    auto* view = column();
    view->setWidthPercentage(100);
    view->setHeight(height);
    view->setPadding(22);
    view->setCornerRadius(18);
    view->setBackgroundColor(themeColor("playwise/card"));
    view->setBorderColor(themeColor("playwise/border"));
    view->setBorderThickness(1);
    view->setShadowType(brls::ShadowType::GENERIC);
    return view;
}

brls::Button* button(const std::string& text, bool primary = false)
{
    auto* view = new brls::Button();
    view->setText(text);
    view->setStyle(primary ? &brls::BUTTONSTYLE_PRIMARY : &brls::BUTTONSTYLE_DEFAULT);
    view->setFontSize(20);
    view->setHeight(58);
    view->setWidthPercentage(100);
    view->setCornerRadius(14);
    view->setHighlightCornerRadius(16);
    view->setFocusable(true);
    return view;
}

class PlayWisePoc final : public brls::Box
{
  public:
    PlayWisePoc()
        : brls::Box(brls::Axis::COLUMN)
    {
        dark = brls::Application::getThemeVariant() == brls::ThemeVariant::DARK;
        this->setWidthPercentage(100);
        this->setHeightPercentage(100);
        rebuild();
    }

  private:
    Page page = Page::CHILD;
    bool dark = false;
    brls::Box* content = nullptr;
    brls::Label* headerTitle = nullptr;
    brls::Label* headerMarker = nullptr;
    brls::Label* navLabel = nullptr;
    brls::Label* footerLabel = nullptr;
    brls::Button* themeButton = nullptr;
    brls::Button* childNavButton = nullptr;
    brls::Button* parentNavButton = nullptr;
    brls::Button* dialogNavButton = nullptr;

    void rebuild()
    {
        clearViews();
        setBackgroundColor(themeColor("playwise/background"));
        setPadding(30, 42, 28, 42);

        auto* header = row(70);
        auto* heading = column();
        heading->setGrow(1);
        headerTitle = label("任我玩 · PlayWise", 30, "playwise/text", 38);
        headerMarker = label(POC_MARKER, 14, "playwise/muted", 24);
        heading->addView(headerTitle);
        heading->addView(headerMarker);
        header->addView(heading);

        themeButton = button(dark ? "切换浅色" : "切换暗色");
        themeButton->setWidth(160);
        themeButton->registerClickAction([this](brls::View*) {
            dark = !dark;
            brls::Application::getPlatform()->setThemeVariant(
                dark ? brls::ThemeVariant::DARK : brls::ThemeVariant::LIGHT);
            applyTheme();
            return true;
        });
        header->addView(themeButton);
        addView(header);

        auto* body = row();
        body->setGrow(1);
        body->setAlignItems(brls::AlignItems::STRETCH);

        auto* nav = column(220);
        nav->setPadding(12, 20, 12, 0);
        navLabel = label("页面", 17, "playwise/muted", 34);
        nav->addView(navLabel);

        childNavButton = button("孩子首页", page == Page::CHILD);
        childNavButton->setMarginBottom(14);
        childNavButton->registerClickAction([this](brls::View*) {
            page = Page::CHILD;
            applyNavigationStyles();
            rebuildContent();
            return true;
        });
        nav->addView(childNavButton);

        parentNavButton = button("家长今日", page == Page::PARENT);
        parentNavButton->setMarginBottom(14);
        parentNavButton->registerClickAction([this](brls::View*) {
            page = Page::PARENT;
            applyNavigationStyles();
            rebuildContent();
            return true;
        });
        nav->addView(parentNavButton);

        dialogNavButton = button("确认弹窗");
        dialogNavButton->registerClickAction([this](brls::View*) {
            openDialog();
            return true;
        });
        nav->addView(dialogNavButton);
        body->addView(nav);

        content = column();
        content->setGrow(1);
        content->setPadding(12, 0, 12, 18);
        body->addView(content);
        addView(body);

        footerLabel = label(
            "方向键移动焦点 · A 确认 · 也可直接触摸按钮 · 此 PoC 不连接 PCTL 或真实数据",
            16, "playwise/muted", 32);
        footerLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        addView(footerLabel);

        rebuildContent();
    }

    void applyNavigationStyles()
    {
        childNavButton->setStyle(page == Page::CHILD
            ? &brls::BUTTONSTYLE_PRIMARY : &brls::BUTTONSTYLE_DEFAULT);
        parentNavButton->setStyle(page == Page::PARENT
            ? &brls::BUTTONSTYLE_PRIMARY : &brls::BUTTONSTYLE_DEFAULT);
        dialogNavButton->setStyle(&brls::BUTTONSTYLE_DEFAULT);
    }

    void applyTheme()
    {
        setBackgroundColor(themeColor("playwise/background"));
        headerTitle->setTextColor(themeColor("playwise/text"));
        headerMarker->setTextColor(themeColor("playwise/muted"));
        navLabel->setTextColor(themeColor("playwise/muted"));
        footerLabel->setTextColor(themeColor("playwise/muted"));
        themeButton->setText(dark ? "切换浅色" : "切换暗色");
        themeButton->setStyle(&brls::BUTTONSTYLE_DEFAULT);
        applyNavigationStyles();
        rebuildContent();
    }

    void rebuildContent()
    {
        content->clearViews();
        if (page == Page::CHILD)
            buildChildPage();
        else
            buildParentPage();
    }

    void buildChildPage()
    {
        content->addView(label("今天还能玩", 20, "playwise/muted", 34));
        content->addView(label("1 小时 24 分", 48, "playwise/accent", 68));
        content->addView(label("今日总额度 2 小时 · 已使用 36 分钟", 18, "playwise/text", 36));

        auto* progress = card(150);
        progress->setMarginTop(18);
        progress->addView(label("今日进度", 22, "playwise/text", 34));
        progress->addView(label(
            "完成作业后可以继续玩。距离今天的休息提醒还有 24 分钟，时间到了会按 Nintendo 家长控制的设置提醒或暂停软件。",
            18, "playwise/muted", 72));
        content->addView(progress);

        auto* actions = card(122);
        actions->setMarginTop(18);
        auto* actionRow = row(74);
        auto* text = column();
        text->setGrow(1);
        text->addView(label("兑换离线加时码", 22, "playwise/text", 34));
        text->addView(label("不需要 Wi-Fi，也不会联系 PlayWise 服务器", 16, "playwise/muted", 30));
        actionRow->addView(text);
        auto* redeem = button("输入代码", true);
        redeem->setWidth(150);
        redeem->registerClickAction([](brls::View*) {
            brls::Application::notify("PoC：触摸和手柄点击均已触发");
            return true;
        });
        actionRow->addView(redeem);
        actions->addView(actionRow);
        content->addView(actions);
    }

    void buildParentPage()
    {
        content->addView(label("家长区 · 今日", 28, "playwise/text", 48));
        content->addView(label("所有内容均为静态样例，不读取真实家庭配置", 17, "playwise/muted", 30));

        auto* metrics = row(156);
        metrics->setMarginTop(18);
        auto* quota = card(150);
        quota->setGrow(1);
        quota->setMarginRight(18);
        quota->addView(label("今日总额度", 18, "playwise/muted", 32));
        quota->addView(label("2 小时", 38, "playwise/accent", 54));
        quota->addView(label("来源：星期日计划", 16, "playwise/muted", 28));
        metrics->addView(quota);

        auto* remain = card(150);
        remain->setGrow(1);
        remain->addView(label("预计剩余", 18, "playwise/muted", 32));
        remain->addView(label("1 小时 24 分", 38, "playwise/success", 54));
        remain->addView(label("同步状态：后台可用", 16, "playwise/muted", 28));
        metrics->addView(remain);
        content->addView(metrics);

        auto* schedule = card(164);
        schedule->setMarginTop(18);
        schedule->addView(label("临时调整今日额度", 22, "playwise/text", 36));
        schedule->addView(label(
            "这是用于检查中文长文本换行、字体回退、圆角边缘和描边连续性的示例。调整只在今天生效，明天会恢复每周计划。",
            17, "playwise/muted", 66));
        auto* apply = button("预览并确认", true);
        apply->setWidth(180);
        apply->registerClickAction([this](brls::View*) {
            openDialog();
            return true;
        });
        schedule->addView(apply);
        content->addView(schedule);
    }

    void openDialog()
    {
        auto* dialog = new brls::Dialog(
            "确认把今天的总额度从 2 小时调整为 2 小时 30 分吗？此 PoC 只检查圆角、阴影、中文排版、手柄焦点和触摸反馈，不会写入任何设置。");
        dialog->getAppletFrame()->setCornerRadius(18);
        dialog->getAppletFrame()->setShadowType(brls::ShadowType::GENERIC);
        dialog->addButton("取消", [] {});
        dialog->addButton("确认（仅演示）", [] {
            brls::Application::notify("PoC：确认操作未写入任何数据");
        });
        dialog->open();
    }
};

void installTheme()
{
    auto& light = brls::Theme::getLightTheme();
    light.addColor("playwise/background", nvgRGB(244, 247, 250));
    light.addColor("playwise/card", nvgRGB(255, 255, 255));
    light.addColor("playwise/border", nvgRGBA(76, 100, 122, 42));
    light.addColor("playwise/text", nvgRGB(26, 36, 47));
    light.addColor("playwise/muted", nvgRGB(91, 107, 122));
    light.addColor("playwise/accent", nvgRGB(35, 110, 211));
    light.addColor("playwise/success", nvgRGB(25, 135, 84));

    auto& darkTheme = brls::Theme::getDarkTheme();
    darkTheme.addColor("playwise/background", nvgRGB(20, 25, 31));
    darkTheme.addColor("playwise/card", nvgRGB(31, 38, 47));
    darkTheme.addColor("playwise/border", nvgRGBA(177, 201, 223, 48));
    darkTheme.addColor("playwise/text", nvgRGB(238, 244, 250));
    darkTheme.addColor("playwise/muted", nvgRGB(166, 181, 195));
    darkTheme.addColor("playwise/accent", nvgRGB(100, 169, 255));
    darkTheme.addColor("playwise/success", nvgRGB(85, 210, 151));
}

} // namespace

int main()
{
    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);
    brls::Logger::info("{}", POC_MARKER);

    if (!brls::Application::init())
        return EXIT_FAILURE;

    brls::Application::createWindow("PlayWise Borealis PoC");
    brls::Application::setGlobalQuit(true);
    installTheme();

    auto* root = new PlayWisePoc();
    brls::Application::pushActivity(new brls::Activity(root));
    while (brls::Application::mainLoop())
        ;

    return EXIT_SUCCESS;
}
