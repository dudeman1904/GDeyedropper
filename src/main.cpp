#include <Geode/Geode.hpp>
#include <Geode/modify/ColorSelectPopup.hpp>

using namespace geode::prelude;

// Full-screen overlay showing a frozen screenshot; tap/drag to sample a colour.
class EyedropperLayer : public CCLayer {
protected:
    Ref<CCImage> m_image;
    CCLayerColor* m_border = nullptr;
    CCLayerColor* m_swatch = nullptr;
    CCLabelBMFont* m_label = nullptr;
    std::function<void(ccColor3B)> m_onPick;
    std::function<void()> m_onCancel;
    ccColor3B m_current = {255, 255, 255};

    bool init(CCImage* image, CCTexture2D* tex,
              std::function<void(ccColor3B)> onPick,
              std::function<void()> onCancel) {
        if (!CCLayer::init()) return false;
        m_image = image;
        m_onPick = std::move(onPick);
        m_onCancel = std::move(onCancel);

        auto win = CCDirector::get()->getWinSize();

        auto spr = CCSprite::createWithTexture(tex);
        auto cs = spr->getContentSize();
        spr->setScaleX(win.width / cs.width);
        spr->setScaleY(win.height / cs.height);
        spr->setPosition(win / 2);
        this->addChild(spr);

        m_border = CCLayerColor::create({0, 0, 0, 255}, 40.f, 40.f);
        m_swatch = CCLayerColor::create({255, 255, 255, 255}, 34.f, 34.f);
        m_swatch->setPosition({3.f, 3.f});
        m_border->addChild(m_swatch);
        m_border->setVisible(false);
        this->addChild(m_border, 10);

        m_label = CCLabelBMFont::create("", "chatFont.fnt");
        m_label->setScale(0.7f);
        m_label->setVisible(false);
        this->addChild(m_label, 10);

        auto hint = CCLabelBMFont::create("Tap to pick a colour (Esc to cancel)", "bigFont.fnt");
        hint->setScale(0.45f);
        hint->setPosition({win.width / 2, win.height - 15.f});
        this->addChild(hint, 10);

        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);
        return true;
    }

    ccColor3B sampleAt(CCPoint const& loc) {
        auto win = CCDirector::get()->getWinSize();
        int w = m_image->getWidth(), h = m_image->getHeight();
        int x = std::clamp(static_cast<int>(loc.x / win.width * w), 0, w - 1);
        int y = std::clamp(static_cast<int>((1.f - loc.y / win.height) * h), 0, h - 1);
        auto data = m_image->getData(); // RGBA, top-down
        auto p = data + (y * w + x) * 4;
        return {p[0], p[1], p[2]};
    }

    void updatePreview(CCTouch* touch) {
        auto loc = touch->getLocation();
        m_current = sampleAt(loc);
        m_swatch->setColor(m_current);
        auto win = CCDirector::get()->getWinSize();
        CCPoint pos = {loc.x - 20.f, loc.y + 30.f};
        pos.x = std::clamp(pos.x, 0.f, win.width - 40.f);
        pos.y = std::clamp(pos.y, 0.f, win.height - 40.f);
        m_border->setPosition(pos);
        m_border->setVisible(true);
        m_label->setString(fmt::format("#{:02X}{:02X}{:02X}", m_current.r, m_current.g, m_current.b).c_str());
        m_label->setPosition({pos.x + 20.f, pos.y - 8.f});
        m_label->setVisible(true);
    }

public:
    static EyedropperLayer* create(CCImage* img, CCTexture2D* tex,
                                   std::function<void(ccColor3B)> onPick,
                                   std::function<void()> onCancel) {
        auto ret = new EyedropperLayer();
        if (ret->init(img, tex, std::move(onPick), std::move(onCancel))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void registerWithTouchDispatcher() override {
        CCTouchDispatcher::get()->addTargetedDelegate(this, -10000, true);
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        updatePreview(touch);
        return true;
    }
    void ccTouchMoved(CCTouch* touch, CCEvent*) override { updatePreview(touch); }
    void ccTouchEnded(CCTouch* touch, CCEvent*) override {
        updatePreview(touch);
        auto cb = m_onPick;
        auto color = m_current;
        this->removeFromParentAndCleanup(true);
        if (cb) cb(color);
    }
    void ccTouchCancelled(CCTouch*, CCEvent*) override {
        auto cb = m_onCancel;
        this->removeFromParentAndCleanup(true);
        if (cb) cb();
    }
    void keyBackClicked() override { this->ccTouchCancelled(nullptr, nullptr); }
};

// Sits on top of the scene and reads the finished frame once, after everything else has drawn.
class EyedropperCapture : public CCNode {
protected:
    std::function<void(CCImage*)> m_done;
    bool m_captured = false;

public:
    static EyedropperCapture* create(std::function<void(CCImage*)> done) {
        auto ret = new EyedropperCapture();
        ret->m_done = std::move(done);
        ret->autorelease();
        return ret;
    }

    void visit() override {
        CCNode::visit();
        if (m_captured) return;
        m_captured = true;

        auto frame = CCDirector::get()->getOpenGLView()->getFrameSize();
        int w = static_cast<int>(frame.width);
        int h = static_cast<int>(frame.height);

        std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
        std::vector<uint8_t> flipped(px.size());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

        // glReadPixels is bottom-up; flip to top-down and force full alpha.
        for (int y = 0; y < h; y++) {
            auto src = px.data() + static_cast<size_t>(h - 1 - y) * w * 4;
            auto dst = flipped.data() + static_cast<size_t>(y) * w * 4;
            std::memcpy(dst, src, static_cast<size_t>(w) * 4);
            for (int x = 0; x < w; x++) dst[x * 4 + 3] = 255;
        }

        auto img = new CCImage();
        img->initWithImageData(flipped.data(), static_cast<int>(flipped.size()),
                               CCImage::kFmtRawData, w, h, 8);

        Ref<EyedropperCapture> self = this;
        auto cb = m_done;
        Loader::get()->queueInMainThread([self, img, cb]() {
            self->removeFromParentAndCleanup(true);
            if (cb) cb(img);
        });
    }
};

class $modify(EyedropperColorPopup, ColorSelectPopup) {
    bool init(EffectGameObject* obj, CCArray* objs, ColorAction* action) {
        if (!ColorSelectPopup::init(obj, objs, action)) return false;

        auto spr = ButtonSprite::create("Eyedrop", 60, true, "bigFont.fnt", "GJ_button_04.png", 24.f, 0.5f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(EyedropperColorPopup::onEyedropper));
        btn->setID("eyedropper-button"_spr);

        // Bottom-right corner of the popup
        auto size = m_mainLayer->getContentSize();
        auto world = m_mainLayer->convertToWorldSpace({size.width - 55.f, 25.f});
        btn->setPosition(m_buttonMenu->convertToNodeSpace(world));
        m_buttonMenu->addChild(btn);
        return true;
    }

    void onEyedropper(CCObject*) {
        auto scene = CCDirector::get()->getRunningScene();

        // Hide every popup (this one and the Edit Object page behind it)
        std::vector<std::pair<Ref<CCNode>, bool>> hidden;
        for (auto child : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (typeinfo_cast<FLAlertLayer*>(child)) {
                hidden.push_back({Ref<CCNode>(child), child->isVisible()});
                child->setVisible(false);
            }
        }
        auto restore = [hidden]() {
            for (auto const& [node, vis] : hidden) node->setVisible(vis);
        };

        Ref<EyedropperColorPopup> self = this;
        auto capture = EyedropperCapture::create([self, restore](CCImage* img) {
            auto tex = new CCTexture2D();
            tex->initWithImage(img);
            tex->autorelease();

            auto layer = EyedropperLayer::create(img, tex,
                [self, restore](ccColor3B c) {
                    restore();
                    self->m_colorPicker->setColorValue(c);
                    self->colorValueChanged(c);
                },
                [restore]() { restore(); }
            );
            img->release(); // layer holds its own Ref
            if (!layer) { restore(); return; }
            CCDirector::get()->getRunningScene()->addChild(layer, 99999);
        });
        scene->addChild(capture, 100000);
    }
};
