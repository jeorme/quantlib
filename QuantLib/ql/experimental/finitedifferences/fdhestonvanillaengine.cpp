/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2008 Andreas Gaida
 Copyright (C) 2008 Ralph Schreyer
 Copyright (C) 2008 Klaus Spanderen

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include <ql/experimental/finitedifferences/fdhestonvanillaengine.hpp>
#include <ql/experimental/finitedifferences/fdmstepconditioncomposite.hpp>
#include <ql/experimental/finitedifferences/fdmamericanstepcondition.hpp>
#include <ql/experimental/finitedifferences/fdmdividendhandler.hpp>
#include <ql/experimental/finitedifferences/fdmhestonvariancemesher.hpp>
#include <ql/experimental/finitedifferences/fdminnervaluecalculator.hpp>
#include <ql/experimental/finitedifferences/fdmlinearoplayout.hpp>
#include <ql/experimental/finitedifferences/fdmmeshercomposite.hpp>
#include <ql/experimental/finitedifferences/fdmblackscholesmesher.hpp>

namespace QuantLib {

    FdHestonVanillaEngine::FdHestonVanillaEngine(
            const boost::shared_ptr<HestonModel>& model,
            Size tGrid, Size xGrid, Size vGrid,
            FdmHestonSolver::FdmSchemeType type, Real theta, Real mu)
    : GenericModelEngine<HestonModel,
                        DividendVanillaOption::arguments,
                        DividendVanillaOption::results>(model),
      tGrid_(tGrid), xGrid_(xGrid), vGrid_(vGrid),
      type_(type), theta_(theta), mu_(mu) {
    }

    void FdHestonVanillaEngine::calculate() const {

        // 1. Layout
        std::vector<Size> dim;
        dim.push_back(xGrid_);
        dim.push_back(vGrid_);
        const boost::shared_ptr<FdmLinearOpLayout> layout(
                                              new FdmLinearOpLayout(dim));

        // 2. Mesher
        const boost::shared_ptr<HestonProcess> process = model_->process();
        const Time maturity = process->time(arguments_.exercise->lastDate());

        // 2.1 The variance mesher
        const Size tGridMin = 10;
        const boost::shared_ptr<FdmHestonVarianceMesher> varianceMesher(
            new FdmHestonVarianceMesher(layout->dim()[1], process, 
                                        maturity,std::max(tGridMin, tGrid_/5)));

        // 2.2 The equity mesher
        const boost::shared_ptr<StrikedTypePayoff> payoff =
            boost::dynamic_pointer_cast<StrikedTypePayoff>(arguments_.payoff);

        const boost::shared_ptr<Fdm1dMesher> equityMesher(
            new FdmBlackScholesMesher(
                xGrid_,
                FdmBlackScholesMesher::processHelper(
                    process->s0(), process->dividendYield(), 
                    process->riskFreeRate(), varianceMesher->volaEstimate()),
                 maturity, payoff->strike(), arguments_.cashFlow));
        
        std::vector<boost::shared_ptr<Fdm1dMesher> > meshers;
        meshers.push_back(equityMesher);
        meshers.push_back(varianceMesher);
        boost::shared_ptr<FdmMesher> mesher(
                                     new FdmMesherComposite(layout, meshers));
        
        // 3. Step conditions
        std::list<boost::shared_ptr<StepCondition<Array> > > stepConditions;
        std::list<std::vector<Time> > stoppingTimes;

        // 3.1 Step condition if discrete dividends
        if(!arguments_.cashFlow.empty()) {
            boost::shared_ptr<FdmDividendHandler> dividendCondition(
                new FdmDividendHandler(arguments_.cashFlow, mesher,
                                       process->riskFreeRate()->referenceDate(),
                                       process->riskFreeRate()->dayCounter(),
                                       0));
            stepConditions.push_back(dividendCondition);
            stoppingTimes.push_back(dividendCondition->dividendTimes());
        }

        // 3.2 Step condition if american exercise
        if (arguments_.exercise->type() == Exercise::American) {
            boost::shared_ptr<FdmInnerValueCalculator> calculator(
                                    new FdmLogInnerValue(arguments_.payoff, 0));
            stepConditions.push_back(boost::shared_ptr<StepCondition<Array> >(
                            new FdmAmericanStepCondition(mesher, calculator)));
        }

        boost::shared_ptr<FdmStepConditionComposite> conditions(
                new FdmStepConditionComposite(stoppingTimes, stepConditions));

        // 4. Boundary conditions
        std::vector<boost::shared_ptr<FdmDirichletBoundary> > boundaries;

        // 5. Solver
        boost::shared_ptr<FdmHestonSolver> solver(new FdmHestonSolver(
                                        Handle<HestonProcess>(process),
                                        mesher, boundaries, conditions,
                                        arguments_.payoff, 
                                        maturity, tGrid_,
                                        type_, theta_, mu_));

        const Real spot = process->s0()->value();
        const Real v0   = process->v0();
        results_.value = solver->valueAt(spot, v0);
        results_.delta = solver->deltaAt(spot, v0);
        results_.gamma = solver->gammaAt(spot, v0);
        results_.theta = solver->thetaAt(spot, v0);
    }
}
