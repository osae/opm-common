/*
  Copyright (C) 2020 Equinor ASA
  Copyright (C) 2020 SINTEF Digital
  This file is part of the Open Porous Media project (OPM).
  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  ?You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <opm/parser/eclipse/Parser/ParserKeywords/A.hpp>

#include <opm/parser/eclipse/EclipseState/Aquifer/NumericalAquifer/NumericalAquiferConnections.hpp>

#include <opm/parser/eclipse/Deck/Deck.hpp>
#include <opm/parser/eclipse/Deck/DeckRecord.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/EclipseGrid.hpp>
#include <opm/common/utility/OpmInputError.hpp>
#include <opm/common/OpmLog/OpmLog.hpp>
#include <fmt/format.h>

#include "../AquiferHelpers.hpp"

#include <string>

namespace Opm {

    NumericalAquiferConnections::NumericalAquiferConnections(const Deck &deck, const EclipseGrid &grid)
    {
        using AQUCON=ParserKeywords::AQUCON;
        if ( !deck.hasKeyword<AQUCON>() ) return;

        const auto& aqucon_keywords = deck.getKeywordList<AQUCON>();
        for (const auto& keyword : aqucon_keywords) {
            OpmLog::info(OpmInputError::format("Initializing numerical aquifer connections from {keyword} in {file} line {line}", keyword->location()));
            for (const auto& record : *keyword) {
                const auto cons_from_record = NumAquiferCon::generateConnections(grid, record);
                for (auto con : cons_from_record) {
                    const size_t aqu_id = con.aquifer_id;
                    const size_t global_index = con.global_index;
                    auto& aqu_cons = this->connections_[aqu_id];
                    if (aqu_cons.find(global_index) == aqu_cons.end()) {
                        aqu_cons.insert({global_index, con});
                    } else {
                        auto error = fmt::format("Numerical aquifer cell at ({}, {}, {}) is declared more than once"
                                                 " for numerical aquifer {}",
                                                 con.I + 1, con.J + 1, con.K + 1, con.aquifer_id);
                        throw OpmInputError(error, keyword->location());
                    }
                }
            }
        }
    }

    const std::map<size_t, NumAquiferCon>&
    NumericalAquiferConnections::getConnections(const size_t aqu_id) const {
        const auto& cons = this->connections_.find(aqu_id);
        if (cons == this->connections_.end())  {
            const auto error = fmt::format("Numerical aquifer {} does not have any connections\n", aqu_id);
            throw std::runtime_error(error);
        } else {
            return cons->second;
        }
    }


    // TODO: we should not need all following the information here
    using AQUCON = ParserKeywords::AQUCON;
    NumAquiferCon::NumAquiferCon(const size_t i, const size_t j, const size_t k,
                                 const size_t global_index_in, const bool allow_connection_active, const DeckRecord& record)
    : aquifer_id(record.getItem<AQUCON::ID>().get<int>(0))
    , I(i)
    , J(j)
    , K(k)
    , global_index(global_index_in)
    , face_dir(FaceDir::FromString(record.getItem<AQUCON::CONNECT_FACE>().getTrimmedString(0)))
    , trans_multipler(record.getItem<AQUCON::TRANS_MULT>().get<double>(0))
    , trans_option(record.getItem<AQUCON::TRANS_OPTION>().get<int>(0))
    , connect_active_cell(allow_connection_active)
    , ve_frac_relperm(record.getItem<AQUCON::VEFRAC>().get<double>(0))
    , ve_frac_cappress(record.getItem<AQUCON::VEFRACP>().get<double>(0))
    {
    }

    std::vector<NumAquiferCon> NumAquiferCon::generateConnections(const EclipseGrid& grid, const DeckRecord& record) {
        std::vector<NumAquiferCon> cons;

        const size_t i1 = record.getItem<AQUCON::I1>().get<int>(0) - 1;
        const size_t j1 = record.getItem<AQUCON::J1>().get<int>(0) -1;
        const size_t k1 = record.getItem<AQUCON::K1>().get<int>(0) - 1;
        const size_t i2 = record.getItem<AQUCON::I2>().get<int>(0) - 1;
        const size_t j2 = record.getItem<AQUCON::J2>().get<int>(0) -1;
        const size_t k2 = record.getItem<AQUCON::K2>().get<int>(0) - 1;

        const std::string str_allow_internal_cells = record.getItem<AQUCON::ALLOW_INTERNAL_CELLS>().getTrimmedString(0);
        // whether the connection face can connect to active/internal cells
        // by default NO, which means basically the aquifer should be outside of the reservoir
        const bool allow_internal_cells = DeckItem::to_bool(str_allow_internal_cells);
        const FaceDir::DirEnum face_dir
                = FaceDir::FromString(record.getItem<AQUCON::CONNECT_FACE>().getTrimmedString(0));

        for (size_t k = k1; k <= k2; ++k) {
            for (size_t j = j1; j <=j2; ++j) {
                for (size_t i = i1; i <= i2; ++i) {
                    // TODO: we probably should give a message here
                    if (!grid.cellActive(i, j, k)) {
                        continue;
                    }
                    /* if (!actnum[grid.getGlobalIndex(i, j, k)]) {
                        continue;
                    }*/
                    if (allow_internal_cells ||
                        !AquiferHelpers::neighborCellInsideReservoirAndActive(grid, i, j, k, face_dir)) {
                        const size_t global_index = grid.getGlobalIndex(i, j, k);
                        cons.emplace_back(NumAquiferCon{i, j, k, global_index, allow_internal_cells, record});
                    }
                }
            }
        }
        return cons;
    }

    bool NumAquiferCon::operator==(const NumAquiferCon& other) const {
        return this->aquifer_id == other.aquifer_id &&
               this->I == other.I &&
               this->J == other.J &&
               this->K == other.K &&
               this->global_index == other.global_index &&
               this->face_dir == other.face_dir &&
               this->trans_multipler == other.trans_multipler &&
               this->trans_option == other.trans_option &&
               this->connect_active_cell == other.connect_active_cell &&
               this->ve_frac_relperm == other.ve_frac_relperm &&
               this->ve_frac_cappress == other.ve_frac_cappress;
    }

}
