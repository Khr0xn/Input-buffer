// Input Buffer (SOLO PRUEBAS) - mod de Geode
// Etapas (todos / wave, en ticks): 2/2 -> 4/4 -> 8/8 -> 16/8 -> OFF
// Un botón en pantalla cicla las etapas (en Android no hay F6).
// Al completar un nivel con el buffer activo se muestra una advertencia.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace buf {
    constexpr int STAGES = 5;              // 0..3 activas, 4 = OFF
    constexpr int ALL[STAGES]  = {2, 4, 8, 16, 0};
    constexpr int WAVE[STAGES] = {2, 4, 8, 8, 0};

    inline int stage = 4;                  // arranca en OFF
    inline bool used = false;              // ¿se usó el buffer en este intento?

    inline bool off() { return stage >= 4; }

    inline std::string label() {
        if (off()) return "Buf OFF";
        return fmt::format("Buf {}/{}", ALL[stage], WAVE[stage]);
    }
}

class $modify(BufPlayer, PlayerObject) {
    struct Fields {
        int tick = 0;
        bool held = false;

        bool queued = false;
        int queuedAt = 0;

        int pressTick = 0;
        bool pendingRelease = false;
        int releaseAt = 0;
    };

    void clearBuf() {
        m_fields->held = false;
        m_fields->queued = false;
        m_fields->pendingRelease = false;
    }

    bool pushButton(PlayerButton btn) {
        if (btn != PlayerButton::Jump || buf::off() || !PlayLayer::get()) {
            return PlayerObject::pushButton(btn);
        }

        m_fields->held = true;
        buf::used = true;

        if (m_isDart) {
            if (m_fields->pendingRelease) {
                m_fields->pendingRelease = false;
                m_fields->pressTick = m_fields->tick;
                return true;
            }
            m_fields->pressTick = m_fields->tick;
            return PlayerObject::pushButton(btn);
        }

        bool needsGround = !m_isShip && !m_isBird && !m_isSwing;
        if (needsGround && !m_isOnGround) {
            m_fields->queued = true;
            m_fields->queuedAt = m_fields->tick;
        }
        return PlayerObject::pushButton(btn);
    }

    bool releaseButton(PlayerButton btn) {
        if (btn != PlayerButton::Jump || buf::off() || !PlayLayer::get()) {
            return PlayerObject::releaseButton(btn);
        }

        m_fields->held = false;

        if (m_isDart) {
            int n = buf::WAVE[buf::stage];
            if (m_fields->tick - m_fields->pressTick < n) {
                m_fields->pendingRelease = true;
                m_fields->releaseAt = m_fields->pressTick + n;
                return false;
            }
        }
        return PlayerObject::releaseButton(btn);
    }

    void update(float dt) {
        PlayerObject::update(dt);
        if (!PlayLayer::get()) return;

        m_fields->tick++;

        if (m_fields->pendingRelease && m_fields->tick >= m_fields->releaseAt) {
            m_fields->pendingRelease = false;
            PlayerObject::releaseButton(PlayerButton::Jump);
        }

        if (m_fields->queued) {
            int age = m_fields->tick - m_fields->queuedAt;
            int window = buf::off() ? 0 : buf::ALL[buf::stage];

            if (m_isOnGround) {
                m_fields->queued = false;
                if (window > 0 && age <= window && !m_fields->held) {
                    PlayerObject::pushButton(PlayerButton::Jump);
                    PlayerObject::releaseButton(PlayerButton::Jump);
                }
            } else if (age > window) {
                m_fields->queued = false;
            }
        }
    }
};

class $modify(BufLayer, PlayLayer) {
    struct Fields {
        ButtonSprite* spr = nullptr;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        buf::stage = Mod::get()->getSavedValue<int>("stage", 4);
        buf::used = false;

        auto spr = ButtonSprite::create(buf::label().c_str());
        m_fields->spr = spr;

        auto item = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(BufLayer::onBuf)
        );
        item->setScale(0.5f);

        auto win = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setID("input-buffer-menu"_spr);
        menu->setPosition({win.width / 2.f, win.height - 18.f});
        menu->addChild(item);
        m_uiLayer->addChild(menu, 100);

        return true;
    }

    void onBuf(CCObject*) {
        buf::stage = (buf::stage + 1) % buf::STAGES;
        Mod::get()->setSavedValue("stage", buf::stage);
        if (m_fields->spr) m_fields->spr->setString(buf::label().c_str());
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        buf::used = false;
        if (m_player1) static_cast<BufPlayer*>(m_player1)->clearBuf();
        if (m_player2) static_cast<BufPlayer*>(m_player2)->clearBuf();
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (buf::used) {
            FLAlertLayer::create(
                "Input Buffer activo",
                "Este nivel se completó con el input buffer activado.\n"
                "Es un <cr>cheat</c>: <cr>no es válido para rankeds</c> "
                "ni para enviar récords.",
                "OK"
            )->show();
        }
    }
};
