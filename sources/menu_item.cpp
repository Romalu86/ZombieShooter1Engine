#include "menu_item.h"
#include "engine.h"
#include "sprite_act_const.h"
#include "vid/vid.h"
#include "map.h"
#include "win/application_win.h"
#include "graph.h"
#include "core/application.h"
#include "core/as_string.h"
#include "core/base_stream.h"
#include "core/resource.h"
#include "core/log.h"
#include "core/profile_p.h"
#include "menu.h"
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <cstdlib>

namespace as1
{
    namespace
    {
        const char kMenuItemEmptyString[] = "";
    }


    STEXT::STEXT(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : FRAME(owner, vid, xyz, dir, parent)
    {
        m_text = STRING::SharedEmptyText();
        m_textClass = STRING::SharedEmptyText();
        m_textLength = 0;
        m_textFlags = 0;
        m_textState84 = 1;
        m_textState88 = 0;
    }

    STEXT::~STEXT()
    {
        if (m_textClass && m_textClass != STRING::SharedEmptyText())
            ::operator delete(m_textClass);
        m_textClass = STRING::SharedEmptyText();
        if (m_text && m_text != STRING::SharedEmptyText())
            ::operator delete(m_text);
        m_text = STRING::SharedEmptyText();
    }

    __forceinline
    void STEXT::assignText(const char* text)
    {
        if (m_text && m_text != STRING::SharedEmptyText())
            ::operator delete(m_text);
        if (!text || *text == '\0')
        {
            m_text = STRING::SharedEmptyText();
        }
        else
        {
            const std::size_t len = std::strlen(text);
            m_text = static_cast<char*>(::operator new(len + 1));
            std::memcpy(m_text, text, len + 1);
        }
        m_textLength = static_cast<int>(std::strlen(m_text));
    }

    __forceinline
    void STEXT::assignTextClass(const char* text)
    {
        if (m_textClass && m_textClass != STRING::SharedEmptyText())
            ::operator delete(m_textClass);
        if (!text || *text == '\0')
        {
            m_textClass = STRING::SharedEmptyText();
        }
        else
        {
            const std::size_t len = std::strlen(text);
            m_textClass = static_cast<char*>(::operator new(len + 1));
            std::memcpy(m_textClass, text, len + 1);
        }
    }


    int STEXT::CalcTextProperty() noexcept
    {
        const char* const text = m_text ? m_text : kMenuItemEmptyString;
        int length = 0;
        int lineStart = 0;
        m_textState84 = 1;
        m_textState88 = 0;

        while (text[length] != '\0')
        {


            if (text[length] == '<' && std::strncmp(text + length, "<Font=", 6) == 0)
            {
                const char* const close = std::strchr(text, '>');
                const int prefixLength = close
                    ? static_cast<int>(close - text)
                    : static_cast<int>(std::strlen(text));
                length += prefixLength;
                lineStart = length;
            }

            if (text[length] == '\n')
            {
                const int columns = length - lineStart;
                if (columns > m_textState88)
                    m_textState88 = columns;
                ++m_textState84;
                lineStart = length + 1;
            }
            ++length;
        }

        m_textLength = length;
        if (m_textState88 == 0)
            m_textState88 = length - lineStart;
        return length;
    }


    int FRAME::Action(int opcode, std::intptr_t actionArgument1, int actionArgument2, int actionArgument3)
    {
        if (opcode >= 0 && opcode <= 5)
            return 0;

        if (opcode == 0x82)
        {
            const int animation = Animation();
            if (animation >= 15)
                return 0;
            if (animation == 4)
            {
                ChangeAnimation(2);
                setRuntimeFlags(runtimeFlags() | 0x00000200u);
            }
            else if (animation == 5)
            {
                ChangeAnimation(3);
                setRuntimeFlags(runtimeFlags() | 0x00000200u);
            }
            else if (animation == 6)
            {

                ChangeAnimation(2);
            }
            else if (animation == 7)
            {

                ChangeAnimation(3);
            }
            if (Animation() == 14)
                ChangeAnimation(0);
            return 0;
        }

        return SPRITE::Action(opcode, actionArgument1,
                          static_cast<int>(actionArgument2),
                          static_cast<int>(actionArgument3));
        }

    int STEXT::Action(int opcode, std::intptr_t actionArgument1, int actionArgument2, int actionArgument3)
    {
        switch (opcode)
        {
        case 0x50:
        {
            auto* stream = reinterpret_cast<BaseStream*>(actionArgument1);
            FRAME::Action(opcode, actionArgument1, static_cast<int>(actionArgument2), static_cast<int>(actionArgument3));
            stream->write_new(&m_textFlags, sizeof(m_textFlags));
            STRING(m_textClass).Write(stream);
            return 0;
        }

        case 0x51:
        {
            auto* stream = reinterpret_cast<BaseStream*>(actionArgument1);
            FRAME::Action(opcode, actionArgument1, static_cast<int>(actionArgument2), static_cast<int>(actionArgument3));
            stream->read_new(&m_textFlags, sizeof(m_textFlags));
            STRING tmpClass;
            readStringLineFromStream(tmpClass, stream);

            return Action(static_cast<int>(ActionCode::ACT_SET_TEXT), reinterpret_cast<std::intptr_t>(&tmpClass), 0, 0);
        }

        case 0x5E:
            return m_textFlags;

        case 0x5F:
            m_textFlags = static_cast<int>(actionArgument1);
            return 0;

        case static_cast<int>(ActionCode::ACT_SET_TEXT):
        {
            const STRING* const requestedClassOwner =
                reinterpret_cast<const STRING*>(actionArgument1);
            const STRING requestedClassCopy(
                requestedClassOwner ? requestedClassOwner->c_str() : kMenuItemEmptyString);
            const char* const requestedClass = requestedClassCopy.c_str();

            assignTextClass(requestedClass);

            const int mode = m_textFlags & 0x70;

            if (mode == 0x10)
            {
                STRING section("menu");
                STRING key(m_textClass);
                STRING defaultValue(m_textClass);
                STRING out;
                core::profile_p::readProfileStringInto(out, *core::g_startupStringsIniPathOwner, section, key, defaultValue);
                assignText(out.c_str());
                CalcTextProperty();
                return 0;
            }

            if (mode == 0x20)
            {
                STRING loaded;
                loadStringFromFile(loaded, reinterpret_cast<const STRING*>(&m_textClass));
                assignText(loaded.c_str());
                CalcTextProperty();
                return 0;
            }

            if (mode == 0)
            {
                assignText(m_textClass);
                CalcTextProperty();
                return 0;
            }

            STRING expanded = Map->ScriptVariable(STRING(m_textClass));
            assignText(expanded.c_str());

            if (mode == 0x40)
            {
                STRING loaded;
                loadStringFromFile(loaded, reinterpret_cast<const STRING*>(&m_text));
                assignText(loaded.c_str());
                CalcTextProperty();
                return 0;
            }

            if (mode == 0x50)
            {
                STRING section("menu");
                STRING key(m_text);
                STRING defaultValue(m_text);
                STRING out;
                core::profile_p::readProfileStringInto(out, *core::g_startupStringsIniPathOwner, section, key, defaultValue);
                assignText(out.c_str());
                CalcTextProperty();
                return 0;
            }

            CalcTextProperty();
            return 0;
        }

        case static_cast<int>(ActionCode::ACT_GET_TEXT):
            return static_cast<int>(reinterpret_cast<std::intptr_t>(&m_text));

        case static_cast<int>(ActionCode::ACT_GET_TEXT_DESC):
            return static_cast<int>(reinterpret_cast<std::intptr_t>(&m_textClass));

        case static_cast<int>(ActionCode::ACT_SET_TEXT_COUNT):
            m_textLength = static_cast<int>(actionArgument1);
            return 0;

        case static_cast<int>(ActionCode::ACT_SET_FILE):
        {
            STRING tmp;
            loadStringFromFile(tmp, reinterpret_cast<const STRING*>(actionArgument1));
            assignText(tmp.c_str());
            return 0;
        }

        case 0x82:
        {
            const char* const text = m_text;
            if (std::strcmp(text, kMenuItemEmptyString) == 0)
                return 0;
            if (actionTimer() != 0)
                return 0;

            const int textLength = static_cast<int>(static_cast<std::int16_t>(std::strlen(text)));
            int cursor = m_textLength;
            if (cursor < textLength)
            {
                m_textLength = cursor + 1;
                const int ch = static_cast<int>(static_cast<signed char>(text[cursor]));
                if (!std::isspace(ch))
                {
                    ChangeAnimation(1);
                    setRuntimeFlags(runtimeFlags() & ~0x00000200u);
                    return 0;
                }

                int sawNewLine = 0;
                while (m_textLength < textLength)
                {
                    cursor = m_textLength;
                    if (text[cursor] == '\n')
                        sawNewLine = 1;
                    m_textLength = cursor + 1;
                    const int nextCh = static_cast<int>(static_cast<signed char>(text[cursor]));
                    if (!std::isspace(nextCh))
                        break;
                }

                if (sawNewLine)
                {
                    if (m_textLength < textLength)
                        --m_textLength;
                    setActionTimer(0x96u);
                    ChangeAnimation(2);
                    return 0;
                }

                ChangeAnimation(1);
                setRuntimeFlags(runtimeFlags() & ~0x00000200u);
                return 0;
            }

            if (Animation() != 0)
                ChangeAnimation(0);
            return 0;
        }

        default:
            return FRAME::Action(opcode, actionArgument1, static_cast<int>(actionArgument2), static_cast<int>(actionArgument3));
        }
        }

    void STEXT::Draw()
    {
        VID* fontVid = Vid();
        if (fontVid && fontVid != EmptyVid && fontVid->directionCount() > 0x7E)
        {
            const int savedFrame = currentFrame();
            const float savedX = X();
            const float savedY = Y();

            if ((m_textFlags & 0x70) == 0x60)
            {
                STRING expanded = Map->ScriptVariable(STRING(m_textClass));
                assignText(expanded.c_str());
                CalcTextProperty();
            }

            if (std::strcmp(m_text, kMenuItemEmptyString) == 0)
                return;

            const int flags = m_textFlags;
            const float glyphWidth = fontVid->sizeX();
            const int columns = m_textState88;
            const int rows = m_textState84;

            if ((flags & 1) != 0)
            {
                setXPosition(X() - static_cast<float>(columns - 1) * glyphWidth * 0.5f);
            }
            else if ((flags & 2) != 0)
            {
                setXPosition(X() - (static_cast<float>(columns - 1) * glyphWidth + fontVid->halfSizeX()));
            }
            else
            {
                setXPosition(X() + fontVid->halfSizeX());
            }

            if ((flags & 8) != 0)
            {
                setYPosition(Y() - static_cast<float>(rows - 1) * fontVid->sizeY() * 0.5f);
            }
            else if ((flags & 4) != 0)
            {
                setYPosition(Y() - (static_cast<float>(rows - 1) * fontVid->sizeY() + fontVid->halfSizeY()));
            }
            else
            {
                setYPosition(Y() + fontVid->halfSizeY());
            }

            float lineStartX = X() - fontVid->sizeX();
            VID* currentFont = fontVid;
            const core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
            auto resolveFont = [&](int nvid) -> VID*
            {
                if (nvid >= 0 && nvid < vidTable.count())
                {
                    if (VID* const resolved = vidTable.slot(nvid))
                        return resolved;
                }
                return EmptyVid;
            };

            int i = 0;
            while (i < m_textLength && m_text[i] != '\0')
            {
                const char* const text = m_text;
                const unsigned char ch = static_cast<unsigned char>(text[i]);

                if (ch == '\n')
                {
                    setXPosition(lineStartX);
                    setYPosition(Y() + currentFont->sizeY());
                }
                else if (ch == '\r')
                {
                    setXPosition(lineStartX);
                }
                else if (ch == '\t')
                {
                    setXPosition(X() + currentFont->sizeX() * 7.0f);
                }
                else if (ch == '<' && std::strncmp(text + i, "<Font=", std::strlen("<Font=")) == 0)
                {


                    const char* const fontRun = text + i;
                    const char* const close = std::strchr(fontRun, '>');
                    if (close)
                    {
                        const int fontNVid = script::ParseStackIntegerText(fontRun + std::strlen("<Font="));
                        VID* const previousFont = currentFont;
                        currentFont = resolveFont(fontNVid);


                        if ((flags & 1) != 0)
                        {
                            const float halfLineGlyphs = static_cast<float>(columns - 1) * 0.5f;
                            setXPosition(X() + halfLineGlyphs *
                                (previousFont->sizeX() - currentFont->sizeX()));
                        }

                        setXPosition(X() - currentFont->sizeX());
                        i = static_cast<int>(close - text);
                    }
                }
                else if (ch == 0x1B && i + 1 < m_textLength)
                {
                    const int unsignedFontIndex = static_cast<unsigned char>(text[i + 1]);
                    if (unsignedFontIndex < vidTable.count() &&
                        vidTable.slot(unsignedFontIndex) != nullptr)
                    {
                        ++i;
                        const int signedFontNVid = static_cast<signed char>(text[i]);
                        currentFont = resolveFont(signedFontNVid);
                        setXPosition(X() - currentFont->sizeX());
                    }
                }
                else if (ch >= 0x20)
                {
                    setCurrentFrameDirect(static_cast<int>(ch));
                    currentFont->Draw(this);
                }

                ++i;
                setXPosition(X() + currentFont->sizeX());
            }

            setXPosition(savedX);
            setYPosition(savedY);
            setCurrentFrameDirect(savedFrame);
            return;
        }

        (void)Graph->drawTextColored(X(), Y(), m_text, 0xFFFFFFFFu);
    }

}
