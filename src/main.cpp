// Input Buffer + Wave Spam Assist (SOLO PRUEBAS) - mod de Geode
//
// 1) Input Buffer: etapas (todos / wave, en ticks) 2/2 -> 4/4 -> 8/8 -> 16/8 -> OFF
// 2) Wave Spam Assist: manteniendo el dedo pulsado en el wave, el mod alterna
//    solo pulsar/soltar. Etapas: 3t -> 2t -> 1t -> OFF (t = ticks por mitad de ciclo).
//
// Dos botones en pantalla ciclan las etapas. Ambos son CHEATS: se avisa al
// activar el spam por primera vez y al completar un nivel con ayudas activas.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace buf {
    constexpr int STAGES = 5;              // 0..3 activas, 4 = OFF
    constexpr int ALL[STAGES]  = {2, 4, 8, 16, 0};
    constexpr int WAVE[STAGES] = {2, 4, 8, 8, 0};

    inline int stage = 4;
    inline bool used = false;

    inline bool off() { return stage >= 4; }

    inline std::string label() {
        if (off()) return "Buf OFF";
        return fmt::format("Buf {}/{}", ALL[stage], WAVE[stage]);
    }
}

namespace spam {
    constexpr int STAGES = 4;              // 0..2 activas, 3 = OFF
    constexpr int PERIOD[STAGES] = {3, 2, 1, 0}; // ticks por mitad de ciclo

    inline int stage = 3;
    inline bool used = false;

    inline bool off() { return stage >= 3; }

    inline std::string label() {
        if (off()) return "Spam OFF";
        return fmt::format("Spam {}t", PERIOD[stage]);
    }
}

// ---------------------------------------------------------------
// Jugador
// ---------------------------------------------------------------
class $modify(BufPlayer, PlayerObject) {
    struct Fields {
        int tick = 0;
        bool held = false;          // botón físicamente pulsado

        // buffer: click adelantado (cubo, bola, robot, araña)
        bool queued = false;
        int queuedAt = 0;

        // buffer en wave: pulsación mínima de N ticks
        int pressTick = 0;
        bool pendingRelease = false;
        int releaseAt = 0;

        // spam en wave
        bool spamDown = false;
        int spamCounter = 0;
    };

    void clearBuf() {
        m_fields->held = false;
        m_fields->queued = false;
        m_fields->pendingRelease = false;
        m_fields->spamDown = false;
        m_fields->spamCounter = 0;
    }

    bool pushButton(PlayerButton btn) {
        bool bufOn = !buf::off();
        bool spamOn = !spam::off();

        if (btn != PlayerButton::Jump || (!bufOn && !spamOn) || !PlayLayer::get()) {
            return PlayerObject::pushButton(btn);
        }

        m_fields->held = true;

        // Wave con spam: el spam tiene prioridad sobre el buffer de wave
        if (m_isDart && spamOn) {
            spam::used = true;
            m_fields->pendingRelease = false;
            m_fields->spamDown = true;
            m_fields->spamCounter = 0;
            return PlayerObject::pushButton(btn);
        }

        if (!bufOn) return PlayerObject::pushButton(btn);
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
        bool bufOn = !buf::off();
        bool spamOn = !spam::off();

        if (btn != PlayerButton::Jump || (!bufOn && !spamOn) || !PlayLayer::get()) {
            return PlayerObject::releaseButton(btn);
        }

        m_fields->held = false;

        if (m_isDart && spamOn) {
            // el dedo se levantó: parar el spam y soltar de verdad
            m_fields->spamDown = false;
            m_fields->spamCounter = 0;
            return PlayerObject::releaseButton(btn);
        }

        if (m_isDart && bufOn) {
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

        // --- Wave Spam Assist: alternar pulsar/soltar mientras el dedo está abajo
        if (m_isDart && !spam::off() && m_fields->held) {
            m_fields->spamCounter++;
            if (m_fields->spamCounter >= spam::PERIOD[spam::stage]) {
                m_fields->spamCounter = 0;
                if (m_fields->spamDown) {
                    PlayerObject::releaseButton(PlayerButton::Jump);
                    m_fields->spamDown = false;
                } else {
                    PlayerObject::pushButton(PlayerButton::Jump);
                    m_fields->spamDown = true;
                }
            }
        }

        // --- Buffer: soltar el wave cuando se cumple la pulsación mínima
        if (m_fields->pendingRelease && m_fields->tick >= m_fields->releaseAt) {
            m_fields->pendingRelease = false;
            PlayerObject::releaseButton(PlayerButton::Jump);
        }

        // --- Buffer: click adelantado
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

// ---------------------------------------------------------------
// PlayLayer: botones, reinicio y advertencias
// ---------------------------------------------------------------
class $modify(BufLayer, PlayLayer) {
    struct Fields {
        ButtonSprite* sprBuf = nullptr;
        ButtonSprite* sprSpam = nullptr;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        buf::stage = Mod::get()->getSavedValue<int>("stage", 4);
        spam::stage = Mod::get()->getSavedValue<int>("spam", 3);
        buf::used = false;
        spam::used = false;

        auto sprBuf = ButtonSprite::create(buf::label().c_str());
        m_fields->sprBuf = sprBuf;
        auto itemBuf = CCMenuItemSpriteExtra::create(
            sprBuf, this, menu_selector(BufLayer::onBuf)
        );
        itemBuf->setScale(0.5f);

        auto sprSpam = ButtonSprite::create(spam::label().c_str());
        m_fields->sprSpam = sprSpam;
        auto itemSpam = CCMenuItemSpriteExtra::create(
            sprSpam, this, menu_selector(BufLayer::onSpam)
        );
        itemSpam->setScale(0.5f);

        auto win = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setID("input-buffer-menu"_spr);
        menu->setPosition({0.f, 0.f});
        itemBuf->setPosition({win.width / 2.f - 35.f, win.height - 18.f});
        itemSpam->setPosition({win.width / 2.f + 35.f, win.height - 18.f});
        menu->addChild(itemBuf);
        menu->addChild(itemSpam);
        m_uiLayer->addChild(menu, 100);

        return true;
    }

    void onBuf(CCObject*) {
        buf::stage = (buf::stage + 1) % buf::STAGES;
        Mod::get()->setSavedValue("stage", buf::stage);
        if (m_fields->sprBuf) m_fields->sprBuf->setString(buf::label().c_str());
    }

    void onSpam(CCObject*) {
        bool wasOff = spam::off();
        spam::stage = (spam::stage + 1) % spam::STAGES;
        Mod::get()->setSavedValue("spam", spam::stage);
        if (m_fields->sprSpam) m_fields->sprSpam->setString(spam::label().c_str());

        static bool warned = false;
        if (wasOff && !spam::off() && !warned) {
            warned = true;
            FLAlertLayer::create(
                "Wave Spam Assist",
                "Esta función hace el spam del wave por ti mientras mantienes "
                "el dedo pulsado.\n"
                "Es un <cr>cheat</c>: úsalo <cy>solo para pruebas</c>. "
                "<cr>No es válido para rankeds</c>, récords ni verificaciones.",
                "Entendido"
            )->show();
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        buf::used = false;
        spam::used = false;
        if (m_player1) static_cast<BufPlayer*>(m_player1)->clearBuf();
        if (m_player2) static_cast<BufPlayer*>(m_player2)->clearBuf();
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (buf::used || spam::used) {
            std::string what;
            if (buf::used) what += "el input buffer";
            if (spam::used) {
                if (!what.empty()) what += " y ";
                what += "el wave spam asistido";
            }
            auto msg = fmt::format(
                "Este nivel se completó con {} activado.\n"
                "Son <cr>cheats</c>: <cr>no es válido para rankeds</c> "
                "ni para enviar récords.",
                what
            );
            FLAlertLayer::create("Ayudas activas", msg, "OK")->show();
        }
    }
};
