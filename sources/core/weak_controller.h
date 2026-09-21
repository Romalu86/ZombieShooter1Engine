#pragma once

#include <cstdint>
#include <array>



namespace as1
{
    class SPRITE;
    class RESOURCE;
    struct ANGLE;

    namespace core
    {
        class R_DOT;
        class R_MAP;
        struct R_POS;


        class R_DOT
        {
        public:
            struct Link
            {
                R_DOT* target;
                std::uint32_t length;
                int reciprocalIndex;
                std::uint32_t crossingLinkToken;
                std::uint32_t facing;
            };

            R_DOT() noexcept;
            ~R_DOT() noexcept;


            void Release() noexcept;
            void LinkTo(R_DOT* target) noexcept;
            int SearchFacingLimitedPath(int depth, R_DOT* excludedA, R_DOT* excludedB,
                                        SPRITE* routeOwner, unsigned char facing) noexcept;
            float ScreenX() const noexcept;
            float ScreenY() const noexcept;

            int refCount() const noexcept { return m_refCount; }
            void setRefCount(int value) noexcept { m_refCount = value; }
            std::uint32_t pathEventFlag() const noexcept { return m_pathEventFlag; }
            void setPathEventFlag(std::uint32_t value) noexcept { m_pathEventFlag = value; }
            int selectedLinkIndex() const noexcept { return m_selectedLinkIndex; }
            std::uint32_t pushLineValue() const noexcept { return m_pushLineValue; }
            void setPushLineValue(std::uint32_t value) noexcept { m_pushLineValue = value; }
            std::uint32_t routeClassTag() const noexcept { return m_routeClassTag; }
            void setRouteClassTag(std::uint32_t value) noexcept { m_routeClassTag = value; }
            int linkCount() const noexcept { return m_linkCount; }
            unsigned char firstLinkFacing() const noexcept
            {
                return m_linkCount > 0
                    ? static_cast<unsigned char>(m_links[0].facing)
                    : 0;
            }
            SPRITE* ownerSprite() const noexcept { return m_ownerSprite; }
            int x() const noexcept { return m_x; }
            int y() const noexcept { return m_y; }
            int id() const noexcept { return m_id; }

            std::array<Link, 6>& links() noexcept { return m_links; }
            const std::array<Link, 6>& links() const noexcept { return m_links; }
            Link* linkAt(int index) noexcept
            {
                return &m_links[static_cast<std::size_t>(index)];
            }
            const Link* linkAt(int index) const noexcept
            {
                return &m_links[static_cast<std::size_t>(index)];
            }
            void setSelectedLinkIndex(int value) noexcept { m_selectedLinkIndex = value; }
            void setOwnerSprite(SPRITE* value) noexcept { m_ownerSprite = value; }

            int GetLink(R_DOT* target) noexcept;

            int GetLink(ANGLE direct) noexcept;

            void UnLink(R_DOT* target) noexcept;

            int GetPos(int x, int y, int z, int linkIndex) noexcept;

            int GetDistance(int x, int y, int z, int linkIndex) noexcept;

            void SetNearestPos(int x, int y, int z, R_POS* out) noexcept;

            int CanEnginePassTo(int linkIndex, SPRITE* engine) noexcept;

            int FindNewDotWithoutBusyDots() noexcept;
            void SetIfIsBetter(int len, int noStep, int unused, int* out) noexcept;
            int FindNewDot(int backLink, ANGLE direct) noexcept;
            const std::array<int, 6>& pathDepthByEdge() const noexcept { return m_pathDepthByEdge; }
            const std::array<int, 6>& pathCostByEdge() const noexcept { return m_pathCostByEdge; }
            const std::array<int, 6>& pathDurationByEdge() const noexcept { return m_pathDurationByEdge; }

        private:
            friend class R_MAP;
            friend struct R_POS;

            int m_refCount;
            std::uint32_t m_pathEventFlag;
            std::uint32_t m_reservedSlot08;
            int m_selectedLinkIndex;
            std::uint32_t m_pushLineValue;
            std::uint32_t m_routeClassTag;
            int m_linkCount;
            std::array<Link, 6> m_links;

            std::array<int, 6> m_pathDepthByEdge;
            std::array<int, 6> m_pathCostByEdge;
            std::array<int, 6> m_pathDurationByEdge;
            SPRITE* m_ownerSprite;
            int m_x;
            int m_y;
            int m_id;
        };


        struct R_POS
        {

            int DoStep(R_DOT* goalDot, SPRITE* target, SPRITE* engine) noexcept;

            int NoStepToTarget(R_DOT* goalDot, SPRITE* target, unsigned int command, SPRITE* engine) noexcept;

            ANGLE Direct() const noexcept;

            R_DOT* Dot2() const noexcept;

            void Write(RESOURCE* resource) noexcept;

            void Read(RESOURCE* resource) noexcept;

            R_DOT* node;
            int progress;
            int auxiliary;
            int edgeIndex;
        };

        class R_MAP
        {
        public:
            R_MAP() noexcept;
            ~R_MAP() noexcept;

            int minX() const noexcept { return m_minX; }
            int minY() const noexcept { return m_minY; }
            int maxX() const noexcept { return m_maxX; }
            int maxY() const noexcept { return m_maxY; }
            int dotCount() const noexcept { return m_dotCount; }

            R_DOT* FindDot(int x, int y, int id) noexcept;
            R_DOT* CreateDot(float x, float y, float id);

        R_DOT* GetNearestDot(int x, int y) noexcept;

            R_DOT* GetNearestDot(int x, int y, int z) noexcept;

            void AddDotToArray(int gx, int gy, int width, int height, int dotIndex) noexcept;

            void CreateAdditionalDots() noexcept;

            void CreateIntersectedDot(R_DOT* a, R_DOT* b, R_DOT* c, R_DOT* d) noexcept;

            void SetPushLine(int beginX, int beginY, int endX, int endY, int pushFlag) noexcept;

            void DebugDraw() noexcept;

            void PrepareForFindDot(R_DOT* goalDot, SPRITE* target, unsigned int command, SPRITE* engine) noexcept;
            int dotCapacity() const noexcept { return m_dotCapacity; }
            R_DOT* const* dots() const noexcept { return m_dots; }

        private:
            friend class R_DOT;

            int m_minX = 0;
            int m_minY = 0;
            int m_maxX = 0;
            int m_maxY = 0;
            std::uint32_t m_dotListOwnerVtableToken;
            int m_dotCount = 0;
            int m_dotCapacity = 0;
            R_DOT** m_dots = nullptr;
        };


        extern int g_pathResultScore;
        extern int g_pathSecondaryBestCost;
        extern R_DOT* g_pathBestNode;
        extern R_MAP g_rMap;
    }
}
