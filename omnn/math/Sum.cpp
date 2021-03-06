//
// Created by Сергей Кривонос on 25.09.17.
//
#include "Sum.h"

#include "Formula.h"
#include "Fraction.h"
#include "Infinity.h"
#include "Integer.h"
#include "Product.h"
#include "System.h"

#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <execution>
#endif
#include <map>
#include <thread>
#include <type_traits>

#ifndef NDEBUG
# define BOOST_COMPUTE_DEBUG_KERNEL_COMPILATION
#endif
#if __cplusplus >= 201700
#include <random>
namespace std {
template< class It >
void random_shuffle( It f, It l )
{
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(f, l, g);
}
}
#endif
#include <boost/compute.hpp>

namespace omnn{
namespace math {

    namespace
    {
        using namespace std;
        
        type_index order[] = {
            typeid(Sum),
            typeid(Product),
            typeid(Exponentiation),
            typeid(Variable),
            typeid(Integer),
            typeid(Fraction),
        };
        
        auto ob = std::begin(order);
        auto oe = std::end(order);
        
        // inequality should cover all cases
        auto toc = [](const Valuable& x, const Valuable& y) // type order comparator
        {
            auto it1 = std::find(ob, oe, static_cast<type_index>(x));
            assert(it1!=oe); // IMPLEMENT
            auto it2 = std::find(ob, oe, static_cast<type_index>(y));
            assert(it2!=oe); // IMPLEMENT
            return it1 == it2 ? *it1 > *it2 : it1 < it2;
        };
        
        SumOrderComparator soc;
    }
    
    // store order operator
    bool SumOrderComparator::operator()(const Valuable& v1, const Valuable& v2) const
    {
        return v1.OfSameType(v2)
            || (v1.IsProduct() && v2.IsExponentiation())
            || (v2.IsProduct() && v1.IsExponentiation())
            ? v1.IsComesBefore(v2) : toc(v1,v2);
    }

    Sum::iterator Sum::Had(iterator it)
    {
        //return it;
        auto item = *it;
        std::cout << item.str();
        it = std::find(members.begin(), members.end(), item);
        throw "Impossible! Check order comparator error.";
        return it;
    }

    auto Thr = std::thread::hardware_concurrency() << 3;
    
    const Sum::iterator Sum::Add(const Valuable& item, const iterator hint)
    {
        Sum::iterator it = end();
        if(item.IsSum()) {
            auto s = cast(item);
            for(auto& i : *s) {
                auto a = Add(i);
                if (it == end() || soc(*a, *it)) {
                    it = a;
                }
            }
            it = members.begin();
        }
        else
        {
#ifdef _WIN32
            if (members.size() > Thr)
                it = std::find(std::execution::par, members.begin(), members.end(), item);
            else
#endif
                it = members.find(item);

            if(it==end())
                it = base::Add(item, hint);
            else
                Update(it, item*2);

            auto itemMaxVaExp = item.getMaxVaExp();
            if(itemMaxVaExp > maxVaExp)
                maxVaExp = itemMaxVaExp;
        }
        return it;
    }
    
	Valuable Sum::operator -() const
	{
		Sum s;
		for (auto& a : members) 
			s.Add(-a);
		return s;
	}

    void Sum::optimize()
    {
        if (optimized || !optimizations) return;

        if (isOptimizing)
            return;
        isOptimizing = true;

        Valuable w;
        do
        {
            w = *this;
            if (members.size() == 1) {
                Valuable m;
                {m = *members.begin();}
                Become(std::move(m));
                return;
            }

            for (auto it = members.begin(); it != members.end();)
            {
                // optimize member
                auto copy = *it;
                copy.optimize();
                if (copy == 0)
                    Delete(it);
                else if (!it->Same(copy))
                    Update(it, copy);
                else
                    ++it;
            }
            
            if (view == Equation) {
                auto& coVa = getCommonVars();
                if (coVa.size()) {
                    *this /= VaVal(coVa);
                    if (!IsSum()) {
                        isOptimizing = {};
                        return;
                    }
                }
            }
            
            for (auto it = members.begin(); it != members.end();)
            {
                if (it->IsSum()) {
                    for (auto& m : cast(*it)->members)
                    {
                        Add(std::move(m));
                    }
                    Delete(it);
                    continue;
                }
                
                auto it2 = it;
                ++it2;
                Valuable c = *it;
                Valuable mc;
                
                auto up = [&](){
                    mc = -c;
                };

                up();
                
                auto comVaEq = [&]() {
                    auto& ccv = c.getCommonVars();
                    auto ccvsz = ccv.size();
                    auto& itcv = it2->getCommonVars();
                    auto itcvsz = itcv.size();
                    return ccvsz
                        && ccvsz == itcvsz 
                        && std::equal(//TODO:std::execution::par,
                            ccv.cbegin(), ccv.cend(), itcv.cbegin());
                };
                
                for (; it2 != members.end();)
                {
                    if (((c.IsFraction() || c.IsInt()) && it2->IsSimpleFraction())
                        || (it2->IsInt() && (c.IsInt() || c.IsSimpleFraction()))
                        || (c.IsProduct() && mc == *it2)
                        )
                    {
                        c += *it2;
                        Delete(it2);
                        up();
                    }
                    else if(it2->Same(c))
                    {
                        c *= 2;
                        Delete(it2);
                        up();
                    }
                    else if(it2->Same(mc))
                    {
                        c = 0_v;
                        Delete(it2);
                        up();
                    }
                    else if (comVaEq())
                    {
                        auto sum = c.IsProduct() ? c : Product{c};
                        sum += *it2;
                        if (!sum.IsSum()) {
                            c = sum;
                            Delete(it2);
                            up();
                        }
                        else
                            ++it2;
                    }
                    else
                        ++it2;
                }

                if (c==0)
                    Delete(it);
                else if (it->Same(c))
                    ++it;
                else
                    Update(it, c);
            }

            // optimize members
            for (auto it = members.begin(); it != members.end();)
            {
                auto copy = *it;
                copy.optimize();
                if (!it->Same(copy)) {
                    Update(it, copy);
                }
                else
                    ++it;
            }
            
#ifndef NDEBUG
            if (w!=*this) {
//                std::cout << "Sum optimized from \n\t" << w << "\n \t to " << *this << std::endl;
            }
#endif
        } while (w != *this);

        if(IsSum())
        {
            if (members.size() == 0) {
                Become(0_v);
            }
            else if (members.size() == 1) {
                cont::iterator b = members.begin();
                Become(std::move(const_cast<Valuable&>(*b)));
            }
            else if (view==Solving || view == Equation){
                // make coefficients int to use https://simple.wikipedia.org/wiki/Rational_root_theorem
                bool scan;
                do {
                    scan = {};
                    if (IsSum())
                    {
                        for (auto& m : members)
                        {
                            if (m.IsProduct()) {
                                auto p = Product::cast(m);
                                for (auto& m : *p) {
                                    if (m.IsFraction()) {
                                        operator*=(Fraction::cast(m)->getDenominator());
                                        scan = true;
                                        break;
                                    }
                                    else if (m.IsExponentiation()) {
                                        auto e = Exponentiation::cast(m);
                                        auto& ee = e->getExponentiation();
                                        if (ee.IsInt() && ee < 0) {
                                            operator*=(e->getBase() ^ (-ee));
                                            scan = true;
                                            break;
                                        }
                                        else if (ee.IsFraction()) {
                                            auto f = Fraction::cast(ee);
                                            auto& d = f->getDenominator();
                                            auto wo = *p / *e;
                                            Become(((e->getBase() ^ f->getNumerator()) * (wo^d)) - ((-(*this - *p)) ^ d));
                                            scan = true;
                                            break;
                                        }
                                    }
                                }
                                if (scan)
                                    break;
                            }
                            else if (m.IsFraction()) {
                                operator*=(Fraction::cast(m)->getDenominator());
                                scan = true;
                                break;
                            }
                            else if (m.IsExponentiation()) {
                                auto e = Exponentiation::cast(m);
                                auto& ee = e->getExponentiation();
                                if (ee.IsInt() && ee < 0) {
                                    operator*=(e->getBase() ^ (-ee));
                                    scan = true;
                                    break;
                                }
                                else if (ee.IsFraction()) {
                                    auto f = Fraction::cast(ee);
                                    Become((e->getBase() ^ f->getNumerator()) - ((-(*this - m)) ^ f->getDenominator()));
                                    scan = true;
                                    break;
                                }
                            }
                        }
                    }
                } while (scan);
                
                auto it = members.begin();
                auto gcd = it->varless();
                for (; it != members.end(); ++it) {
                    gcd = boost::gcd(gcd, it->varless());
                }
                
                if(gcd!=1_v){
                    operator/=(gcd);
                }
                // todo : make all va exponentiations > 0
            }
            
            if (view == Equation) {
                auto& coVa = getCommonVars();
                if (coVa.size()) {
                    *this /= VaVal(coVa);
                    if (!IsSum()) {
                        isOptimizing = {};
                        return;
                    }
                }
            }
        }
        
        if (IsSum()) {
            isOptimizing = false;
            optimized = true;
        }
    }

    const Valuable::vars_cont_t& Sum::getCommonVars() const
    {
        vars.clear();
        
        auto it = members.begin();
        if (it != members.end())
        {
            vars = it->getCommonVars();
            ++it;
            while (it != members.end()) {
                auto& _ = it->getCommonVars();
                for(auto i = vars.begin(); i != vars.end();)
                {
                    auto found = _.find(i->first);
                    if(found != _.end())
                    {
                        auto mm = std::minmax(i->second, found->second,
                                              [](auto& _1, auto& _2){return _1.abs() < _2.abs();});
                        if ((i->second < 0) == (found->second < 0)) {
                            i->second = mm.first;
                        }
                        else
                        {
                            auto sign = mm.second / mm.second.abs();
                            i->second = sign * (mm.second.abs() - mm.first.abs());
                        }
                        ++i;
                    }
                    else
                    {
                        vars.erase(i++);
                    }
                }
                ++it;
            }
        }

        return vars;
    }
    
    Valuable& Sum::operator +=(const Valuable& v)
    {
        if (v.IsInt() && v == 0) {
            return *this;
        }
        if (v.IsSum()) {
            auto s = cast(v);
            for (auto& i : *s) {
                operator+=(i);
            }
        }
        else
        {
            for (auto it = members.begin(); it != members.end(); ++it)
            {
                if (it->OfSameType(v) && it->getCommonVars() == v.getCommonVars())
                {
                    auto s = *it + v;
                    if (!s.IsSum()) {
                        Update(it, s);
                        optimize();
                        return *this;
                    }
                }
            }

            // add new member
            Add(v);
        }

        optimize();
        return *this;
    }

	Valuable& Sum::operator *=(const Valuable& v)
	{
        Sum sum;
        auto vIsInt = v.IsInt();
        if (vIsInt && v == 0)
        {
        }
        else if (vIsInt && v == 1)
        {
            return *this;
        }
        else if (v.IsSum())
            for(auto& _1 : *cast(v))
				for (auto& _2 : members)
                    sum.Add(_1*_2);
        else
        {
            for (auto& a : members)
            {
                sum.Add(a * v);
            }
            if (vIsInt)
                sum.optimized = optimized;
        }
        Become(std::move(sum));
        return *this;
	}

	Valuable& Sum::operator /=(const Valuable& v)
	{
        Valuable s = 0_v;
		if (v.IsSum())
		{
            auto i = cast(v);
            if(!v.FindVa())
            {
                for (auto& a : members) {
                    for (auto& b : i->members) {
                        s += a / b;
                    }
                }
            }
            else
            {
                if (size() < i->size())
                {
                    s = Fraction(*this, v);
                }
                else if (HasSameVars(v))
                {
                    auto b=i->begin();
                    auto e = i->end();
                    size_t offs = 0;
                    std::deque<Valuable> hist {*this};
                    
                    auto icnt = size() * 2;
                    while(*this != 0_v && icnt--)
                    {
                        if(IsSum())
                        {
                            auto it = begin();
                            for (auto i=offs; it != end() && i--; ) {
                                ++it;
                            }
                            if (it == end()) {
                                IMPLEMENT;
                            }
                            auto vars = it->Vars();
                            auto coVa = it->getCommonVars();
                            auto maxVa = std::max_element(coVa.begin(), coVa.end(),
                                                          [](auto&_1,auto&_2){return _1.second < _2.second;});
                            auto it2 = b;
                            while(it2 != e && it2->Vars() != vars)
                                ++it2;
                            if (it2 == e) {
                                
                                for (it2 = b; it2 != e; ++it2)
                                {
                                    bool found = {};
                                    for(auto& v : it2->Vars())
                                    {
                                        found = v == maxVa->first;
                                        if (found) {
                                            auto coVa2 = it2->getCommonVars();
                                            auto coVa2vIt = coVa2.find(v);
                                            if (coVa2vIt == coVa2.end()) {
                                                IMPLEMENT
                                            }
                                            auto coVa1vIt = coVa.find(v);
                                            if (coVa1vIt == coVa.end()) {
                                                IMPLEMENT
                                            }
                                            found = coVa1vIt->second >= coVa2vIt->second;
                                            
                                        }
                                        
                                        if (!found) {
                                            break;
                                        }
                                    }
                                    
                                    if(found)
                                        break;
                                }
                                
                                if (it2 == e) {
                                    IMPLEMENT;
                                }
                            }

                            auto t = *begin() / *it2;
                            s += t;
                            t *= v;
                            *this -= t;
                            if (std::find(hist.begin(), hist.end(), *this) == hist.end()) {
                                hist.push_back(*this);
                                constexpr size_t MaxHistSize = 8;
                                if (hist.size() > MaxHistSize) {
                                    hist.pop_front();
                                    offs = 0;
                                }
                            }
                            else
                            {
                                ++offs;
                                hist.clear();
                            }
                        }
                        else
                        {
                            s += *this / v;
                            break;
                        }
                    }
                    if (*this != 0_v) {
                        s = Fraction(*this, v);
                    }
                }
                else
                {
                    s = Fraction(*this, v);
                }
            }
		}
		else
		{
            for (auto& a : members) {
                s += a / v;
            }
		}
        return Become(std::move(s));
	}

    Valuable& Sum::d(const Variable& x)
    {
        Valuable sum = 0_v;
        auto add = [&](const Valuable& m)
        {
            if (sum.IsSum()) {
                Sum::cast(sum)->Add(m);
            }
            else
                sum += m;
        };
        for(auto m : *this)
            add(m.d(x));
        
        return Become(std::move(sum));
    }
    
    Sum::Sum(const std::initializer_list<Valuable>& l)
    {
        for (const auto& arg : l)
        {
            auto a = cast(arg);
            if(a)
                for(auto& m: *a)
                    this->Add(m, end());
            else
                this->Add(arg, end());
        }
    }
    
    bool Sum::IsComesBefore(const Valuable& v) const
    {
        if (v.IsSum()) {
            auto sz1 = size();
            auto s = Sum::cast(v);
            auto sz2 = s->size();
            if (sz1 != sz2) {
                return sz1 > sz2;
            }
            
            for (auto i1=begin(), i2=s->begin(); i1!=end(); ++i1, ++i2) {
                if (*i1 != *i2) {
                    return i1->IsComesBefore(*i2);
                }
            }
        }
        
        return {};
    }

	std::ostream& Sum::print(std::ostream& out) const
	{
        std::stringstream s;
        s << '(';
        constexpr char sep[] = " + ";
		for (auto& b : members) 
            s << b << sep;
        auto str = s.str();
        auto cstr = const_cast<char*>(str.c_str());
        cstr[str.size() - sizeof(sep) + 1] = 0;
        out << cstr << ')';
		return out;
	}
    
    Valuable Sum::sqrt() const
    {
        return Exponentiation(*this, 1_v/2);
    }

    Valuable& Sum::sq()
    {
        Sum s;
        auto e = end();
        for (auto i = begin(); i != e; ++i)
        {
            s.Add(i->Sq());
            for (auto j = i; ++j != e;)
            {
                s.Add(*i * *j * 2);
            }
        }
        return Become(std::move(s));
    }

    Valuable Sum::calcFreeMember() const
    {
        Valuable _ = 0_v;
        for(auto& m : *this) {
            _ += m.calcFreeMember();
        }
        return _;
    }

    size_t Sum::FillPolyCoeff(std::vector<Valuable>& coefficients, const Variable& v) const
    {
        size_t grade = 0;
        coefficients.resize(members.size());
        Sum c0;
        auto add = [&](auto i, Valuable&& a) {
            if (i)
                coefficients[i] += a;
            else
                c0.Add(a);
        };
	    // TODO : openmp
	    //#pragma omp parallel default(none) shared(grade,coefficients)
	    {
        //#pragma omp for 
        for (auto& m : members)
        {
            if (m.IsProduct())
            {
                auto& coVa = m.getCommonVars();
                auto it = coVa.find(v);
                auto noVa = it == coVa.end();
                if (noVa && m.HasVa(v))
                {
                    auto s = *this;
                    s.SetView(View::Solving);
                    s.optimize();
                    coefficients.clear();
                    return s.FillPolyCoeff(coefficients, v);
                }

                auto vcnt = noVa ? 0 : it->second; // exponentation of va
                if (!vcnt.IsInt()) {
                    IMPLEMENT
                }
                int ie = static_cast<int>(vcnt);
                if (ie < 0)
                {
                    coefficients.clear();
                    return cast(*this * (v ^ (-ie)))->FillPolyCoeff(coefficients, v);
                }
                else if (ie > grade) {
                    grade = ie;
                    if (ie >= coefficients.size()) {
                        coefficients.resize(ie + 1);
                    }
                }

                add(ie, m / (v^vcnt));
            }
            else
            {
                auto va = Variable::cast(m);
                if (va && *va == v) {
                    ++coefficients[1];
                }
                else
                {
                    auto e = Exponentiation::cast(m);
                    if (e && e->getBase()==v)
                    {
                        auto& ee = e->getExponentiation();
                        if (ee.IsInt()) {
                            auto i = static_cast<decltype(grade)>(ee.ca());
                            if (i > grade) {
                                grade = i;
                                if (i >= coefficients.size()) {
                                    coefficients.resize(i+1);
                                }
                            }
                            add(i, 1);
                        }
                        else
                            IMPLEMENT
                    }
                    else
                    {
                        c0.Add(m);
                    }
                }
            }
        }
        }
        c0.optimized = optimized;
        coefficients[0] = std::move(c0);
        return grade;
    }
    
    Valuable::solutions_t Sum::operator()(const Variable& va, const Valuable& augmentation) const
    {
        solutions_t solutions;
        {
        auto e = *this + augmentation;
        if (!e.IsSum()) {
            IMPLEMENT;
        }
        auto es = cast(e);
        std::vector<Valuable> coefs;
        auto grade = es->FillPolyCoeff(coefs, va);
        if (coefs.size() && grade && grade <= 2)
        {
            es->solve(va, solutions, coefs, grade);
            for (auto i=solutions.begin(); i != solutions.end();) {
                if (i->HasVa(va)) {
                    IMPLEMENT
                    solutions.erase(i++);
                }
                else
                    ++i;
            }
            
            if (solutions.size()) {
                return solutions;
            }
        }
        }
        Valuable _ = augmentation;
        Valuable todo = 0_v;
        for(auto& m : *this)
        {
            if (m.HasVa(va)) {
                todo += m;
            } else {
                _ -= m;
            }
        }
        
        if (todo.IsSum()) {
            auto coVa = todo.getCommonVars();
            auto it = coVa.find(va);
            if (it != coVa.end()) {
                todo /= it->first ^ it->second;
                if (todo.HasVa(va))
                    IMPLEMENT;
                if (it->second < 0) {
                    _ *= it->first ^ -it->second;
                    return _(va, todo);
                }
                else if (todo != 0_v)
                {
                    Valuable solution = Fraction(_, todo);
                    if (it->second != 1_v) {
                        solution ^= 1_v/it->second;
                    }
                    else
                        solution.optimize();
                    solutions.insert(solution);
                }
            }
            else
            {
                auto stodo = cast(todo);
                if (stodo->size() == 2)
                {
                    if(stodo->begin()->IsFraction())
                    {
                        solutions = (*stodo->begin())(va, _ - *stodo->rbegin());
                    }
                    else if(stodo->rbegin()->IsFraction())
                    {
                        solutions = (*stodo->rbegin())(va, _ - *stodo->begin());
                    }
                    else
                    {
                        IMPLEMENT
                    }
                }
                else
                {
                    solutions.insert(0_v);
                    for(auto& m : *stodo)
                    {IMPLEMENT
                        solutions_t incoming(std::move(solutions));
                        for(auto& e : m(va, _))
                        {
                            IMPLEMENT // test it
                            for(auto& s : incoming)
                            {
                                solutions.insert(s + e);
                            }
                        }
                    }
                }
            }
        }
        else
        {
            solutions = todo(va, _);
        }

        return solutions;
    }
    
    Valuable::solutions_t Sum::operator()(const Variable& va) const
    {
        Valuable::solutions_t s;
        
        std::vector<Valuable> coefs;
        auto grade = FillPolyCoeff(coefs, va);
        if (coefs.size() && grade && grade <= 2)
        {
            solve(va, s, coefs, grade);
            for (auto i=s.begin(); i != s.end();) {
                if (i->HasVa(va)) {
                    IMPLEMENT
                    s.erase(i++);
                }
                else
                    ++i;
            }
        }

        if (!s.size())
        {
            Valuable augmentation = 0_v;
            Valuable _ = 0_v;
            for(auto& m : *this)
            {
                if (m.HasVa(va)) {
                    _ += m;
                } else {
                    augmentation -= m;
                }
            }
            s = _(va, augmentation);
        }
        
        return s;
    }
    
    Valuable::solutions_t Sum::GetIntegerSolution(const Variable& va) const
    {
        solutions_t solutions;
        Valuable singleIntegerRoot;
        bool haveMin = false;
        auto _ = *this;
        _.optimize();
        auto sum = cast(_);
        if (!sum) {
            IMPLEMENT
        }
        
        std::vector<Valuable> coefficients;
        auto g = sum->FillPolyCoeff(coefficients,va);
        if (g<5)
        {
            sum->solve(va, solutions, coefficients, g);
            
            if(solutions.size())
            {
                return solutions;
            }
        }
        
        auto extrenums = sum->extrenums(va);
        auto zz = sum->get_zeros_zones(va, extrenums);
        
        Valuable min;
        Valuable closest;
        auto finder = [&](const Integer* i) -> bool
        {
            auto c = _;
            if(!c.IsProduct())
                c.optimize();
            auto cdx = c;
            cdx.d(va);
            std::cout << "searching: f(" << va << ")=" << _ << "; f'=" << cdx << std::endl;
            return i->Factorization([&,c](const Valuable& i)
                                    {
                                        auto _ = c;
                                        _.Eval(va, i);
                                        _.optimize();
                                        
                                        bool found = _ == 0_v;
                                        if (found) {
                                            std::cout << "found " << i << std::endl;
                                            singleIntegerRoot = i;
                                            solutions.insert(i);
                                        }
                                        else
                                        {
                                            auto d_ = cdx;
                                            d_.Eval(va, i);
                                            d_.optimize();
                                            std::cout << "trying " << i << " got " << _ << " f'(" << i << ")=" << d_ << std::endl;
                                            if(!haveMin || _.abs() < min.abs()) {
                                                closest = i;
                                                min = _;
                                                haveMin = true;
                                            }
                                        }
                                        return found;
                                    },
                                    Infinity(),
                                    zz);
        };
        
        auto freeMember = _.calcFreeMember();
        auto i = Integer::cast(freeMember);
        if(!i) {
            IMPLEMENT
        }
        
        if (finder(i)) {
            return solutions;
        }
        
        
        IMPLEMENT

    }
    
    void Sum::solve(const Variable& va, solutions_t& solutions) const
    {
        std::vector<Valuable> coefficients;
        auto grade = FillPolyCoeff(coefficients, va);
        solve(va, solutions, coefficients, grade);
    }
    
    void Sum::solve(const Variable& va, solutions_t& solutions, const std::vector<Valuable>& coefficients, size_t grade) const
    {
        switch (grade) {
            case 1: {
                //x=-(b/a)
                auto& b = coefficients[0];
                auto& a = coefficients[1];
                solutions.insert(-b / a);
                break;
            }
            case 2: {
                // square equation x=(-b+√(b*b-4*a*c))/(2*a)
                auto& a = coefficients[2];
                auto& b = coefficients[1];
                auto& c = coefficients[0];
                auto d = (b ^ 2) - 4_v * a * c;
                auto dsq = d.sqrt();
                auto a2 = a * 2;
                solutions.insert((-dsq-b)/a2);
                solutions.insert((dsq-b)/a2);
                break;
            }
            case 3: {
                // https://en.wikipedia.org/wiki/Cubic_function#General_solution_to_the_cubic_equation_with_real_coefficients
                auto& a = coefficients[3];
                auto& b = coefficients[2];
                auto& c = coefficients[1];
                auto& d = coefficients[0];
                auto asq = a.Sq();
                auto bsq = b.Sq();
                auto ac3 = a * c * 3;
                auto di = bsq*c.Sq() - 4_v*a*(c^3) - 4_v*(b^3)*d - 27_v*asq*d.Sq() + ac3*(18/3)*b*d;
                auto d0 = bsq - ac3;
                if (di == 0)
                {
                    if (d0 == 0)
                    {
                        solutions.insert(b / (a*-3));
                    }
                    else
                    {
                        solutions.insert((a*d * 9 - b * c) / (d0 * 2));
                        solutions.insert((a*b*c * 4 + asq * d*-9 - bsq * b) / (d0 * a));
                    }
                }
                else
                {
                    auto d1 = (bsq * 2 - ac3 * 3)*b + asq * d * 27;
                    auto subC = (asq*di*-27).sqrt();
                    auto C1 = ((d1 + subC) / 2) ^ (1_v / 3);
                    auto C2 = ((d1 - subC) / 2) ^ (1_v / 3);
                    solutions.insert((b + C1 + d0 / C1) / (a*-3));
                    solutions.insert((b + C1 + d0 / C2) / (a*-3));
                    solutions.insert((b + C2 + d0 / C1) / (a*-3));
                    solutions.insert((b + C2 + d0 / C2) / (a*-3));
                }
                break;
            }
            case 4: {
                // four grade equation ax^4+bx^3+cx^2+dx+e=0
                // see https://math.stackexchange.com/questions/785/is-there-a-general-formula-for-solving-4th-degree-equations-quartic
                auto& a = coefficients[4];
                auto& b = coefficients[3];
                auto& c = coefficients[2];
                auto& d = coefficients[1];
                auto& e = coefficients[0];
                auto sa = a*a;
                auto sb = b*b;
                auto p1 = 2*c*c*c-9*b*c*d+27*a*d*d+27*b*b*e-72*a*c*e;
                auto p2 = p1+(4*((c*c-3*b*d+12*a*e)^3)+(p1^2)).sqrt();
                auto qp2 = (p2/2)^(1_v/3);
                p1.optimize();
                p2.optimize();
                qp2.optimize();
                auto p3 = (c*c-3*b*d+12*a*e)/(3*a*qp2)+qp2/(3*a);
                auto p4 = (sb/(4*sa)-(2*c)/(3*a)+p3).sqrt();
                auto p5 = sb/(2*sa)-(4*c)/(4*a)-p3;
                auto p6 = (-sb*b/(sa*a)+4*b*c/sa-8*d/a)/(4*p4);
                auto xp1 = b/(-4*a);
                auto xp2 = p4/2;
                auto xp3_1 = (p5-p6).sqrt()/2;
                auto xp3_2 = (p5+p6).sqrt()/2;
                solutions.insert(xp1-xp2-xp3_1);
                solutions.insert(xp1-xp2+xp3_1);
                solutions.insert(xp1+xp2-xp3_2);
                solutions.insert(xp1+xp2+xp3_2);
                break;
            }
            default: {
                // build OpenCL kernel
                using namespace boost::compute;
                auto copy = *this;
                copy.optimize();
                std::stringstream source;
                source << "__kernel void f(__global long *c) {"
                        << "    const uint i = get_global_id(0);"
                        << "    long " << va << "=i;"
                        << "    c[i] = "; copy.code(source);
                source << ";}";
                
                device cuwinner = system::default_device();
                for(auto& p: system::platforms())
                    for(auto& d: p.devices())
                        if (d.compute_units() > cuwinner.compute_units())
                            cuwinner = d;
                auto wgsz = cuwinner.max_work_group_size();
                context context(cuwinner);
                
                kernel k(program::build_with_source(source.str(), context), "f");
                auto sz = wgsz * sizeof(cl_long);
                buffer c(context, sz);
                k.set_arg(0, c);
                
                command_queue queue(context, cuwinner);
                // run the add kernel
                queue.enqueue_1d_range_kernel(k, 0, wgsz, 0);
                
                // transfer results to the host array 'c'
                std::vector<cl_long> z(wgsz);
                queue.enqueue_read_buffer(c, 0, sz, &z[0]);
                queue.finish();
                
                auto simple = *this;
                auto addSolution = [&](auto& s) -> bool {
                    auto it = solutions.insert(s);
                    if (it.second) {
                        simple /= va - s;
                    }
                    return it.second;
                };
                
                for(auto i = wgsz; i-->0;)
                    if (z[i] == 0) {
                        // lets recheck on host
                        auto copy = *this;
                        copy.Eval(va, i);
                        copy.optimize();
                        if (copy == 0_v) {
                            addSolution(i);
                        }
                    }
                
                if(solutions.size() == grade)
                    return;
                
                auto simpleSolve = [&](){
                    solutions_t news;
                    do {
                        news.clear();
                        simple.solve(va, news);
                        for(auto& s : news)
                        {
                            addSolution(s);
                        }
                    } while(news.size());
                };
                
                auto addSolution2 = [&](auto& _) -> bool {
                    if (addSolution(_)) {
                        simpleSolve();
                    }
                    return solutions.size() == grade;
                };
                
                
#define IS(_) if(addSolution2(_))return;
                
                if (solutions.size()) {
                    simpleSolve();
                }
                
                if(solutions.size() == grade)
                    return;
                
                
                for(auto i : simple.GetIntegerSolution(va))
                {
                    IS(i);
                }
                
                // decomposition
                IMPLEMENT;
//                auto yx = -*this;
//                Variable y;
//                yx.Valuable::Eval(va, y);
//                yx += *this;
//                yx /= va - y;
//                auto _ = yx.str();
                
                sz = grade + 1;
                auto sza = (grade >> 1) + (grade % 2) + 1;
                auto szb = (grade >> 1) + 1;
                std::vector<Variable> vva(sza);
                std::vector<Variable> vvb(szb);
                Valuable eq1, eq2;
                for (auto i = sza; i--; ) {
                    eq1 += vva[i] * (va ^ i);
                }
                for (auto i = szb; i--; ) {
                    eq2 += vvb[i] * (va ^ i);
                }
                
                auto teq = eq1*eq2;
                std::vector<Valuable> teq_coefficients;
                auto teqs = Sum::cast(teq);
                if (teqs) {
                    if (teqs->FillPolyCoeff(teq_coefficients, va) != grade) {
                        IMPLEMENT
                    }
                } else {
                    IMPLEMENT
                }
                System sequs;
                for (auto i = sz; i--; ) {
                    auto c = coefficients[i];
                    Valuable eq = teq_coefficients[i]-c;
                    sequs << eq;
                }
                //sequs << copy;
                std::vector<solutions_t> s(szb);
                for (auto i = szb; i--; ) {
                    s[i] = sequs.Solve(vvb[i]);
                }
                
                auto ss = sequs.Solve(vvb[0]);
                if (ss.size()) {
                    for(auto& s : ss)
                    {
                        copy /= va - s;
                    }
                    copy.solve(va, solutions);
                }
                else
                    IMPLEMENT;
                break;
            }
        }
    }
    
    /** fast linear equation formula deduction */
	Formula Sum::FormulaOfVa(const Variable& v) const
	{
        Valuable fx(0);
        std::vector<Valuable> coefficients(4);
        auto grade = FillPolyCoeff(coefficients,v);
        
        switch (grade) {
            case 2: {
                // square equation axx+bx+c=0
                // root formula: x=((b*b-4*a*c)^(1/2)-b)/(2*a)
                auto& a = coefficients[2];
                auto& b = coefficients[1];
                auto& c = coefficients[0];
                fx = ((b*b-4*a*c).sqrt()-b)/(2*a);
                break;
            }
            case 4: {
                // four grade equation ax^4+bx^3+cx^2+dx+e=0
                // see https://math.stackexchange.com/questions/785/is-there-a-general-formula-for-solving-4th-degree-equations-quartic
                auto& a = coefficients[4];
                auto& b = coefficients[3];
                auto& c = coefficients[2];
                auto& d = coefficients[1];
                auto& e = coefficients[0];
                auto sa = a*a;
                auto sb = b*b;
                auto p1 = 2*c*c*c-9*b*c*d+27*a*d*d+27*b*b*e-72*a*c*e;
                auto p2 = p1+(4*((c*c-3*b*d+12*a*e)^3)+(p1^2)).sqrt();
                auto qp2 = (p2/2)^(1_v/3);
                p1.optimize();
                p2.optimize();
                qp2.optimize();
                auto p3 = (c*c-3*b*d+12*a*e)/(3*a*qp2)+qp2/(3*a);
                auto p4 = (sb/(4*sa)-(2*c)/(3*a)+p3).sqrt();
                auto p5 = sb/(2*sa)-(4*c)/(4*a)-p3;
                auto p6 = (-sb*b/(sa*a)+4*b*c/sa-8*d/a)/(4*p4);
                fx = -b/(4*a)+p4/2+(p5+p6).sqrt()/2;
//                fx.optimize();
                break;
            }
            default: {
                throw "Implement!";
                break;
            }
        }
        
        return Formula::DeclareFormula(v, fx);
	}
    
    
}}
