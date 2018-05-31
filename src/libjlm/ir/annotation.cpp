/*
 * Copyright 2017 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include <jlm/jlm/ir/aggregation.hpp>
#include <jlm/jlm/ir/annotation.hpp>
#include <jlm/jlm/ir/basic-block.hpp>
#include <jlm/jlm/ir/operators/operators.hpp>

#include <algorithm>
#include <functional>
#include <typeindex>

namespace jlm {

demandset::~demandset()
{}

branchset::~branchset()
{}

static void
annotate(const aggnode * node, variableset & pds, demandmap & dm);

static inline std::unique_ptr<demandset>
annotate_basic_block(const basic_block & bb, variableset & pds)
{
	auto ds = demandset::create(pds);
	for (auto it = bb.rbegin(); it != bb.rend(); it++) {
		auto & tac = *it;
		if (is<assignment_op>(tac->operation())) {
			/*
					We need special treatment for assignment operation, since the variable
					they assign the value to is modeled as an argument of the tac.
			*/
			JLM_DEBUG_ASSERT(tac->ninputs() == 2 && tac->noutputs() == 0);
			pds.erase(tac->input(0));
			pds.insert(tac->input(1));
		} else {
			for (size_t n = 0; n < tac->noutputs(); n++)
				pds.erase(tac->output(n));
			for (size_t n = 0; n < tac->ninputs(); n++)
				pds.insert(tac->input(n));
		}
	}
	ds->top = pds;

	return ds;
}

static inline void
annotate_entry(const aggnode * node, variableset & pds, demandmap & dm)
{
	JLM_DEBUG_ASSERT(is<entryaggnode>(node));
	const auto & ea = static_cast<const entryaggnode*>(node)->attribute();

	auto ds = demandset::create(pds);
	for (size_t n = 0; n < ea.narguments(); n++)
		pds.erase(ea.argument(n));

	ds->top = pds;
	JLM_DEBUG_ASSERT(dm.find(node) == dm.end());
	dm[node] = std::move(ds);
}

static inline void
annotate_exit(const aggnode * node, variableset & pds, demandmap & dm)
{
	JLM_DEBUG_ASSERT(is<exitaggnode>(node));
	const auto & xa = static_cast<const exitaggnode*>(node)->attribute();

	auto ds = demandset::create(pds);
	for (size_t n = 0; n < xa.nresults(); n++)
		pds.insert(xa.result(n));

	ds->top = pds;
	JLM_DEBUG_ASSERT(dm.find(node) == dm.end());
	dm[node] = std::move(ds);
}

static inline void
annotate_block(const aggnode * node, variableset & pds, demandmap & dm)
{
	JLM_DEBUG_ASSERT(is<blockaggnode>(node));
	const auto & bb = static_cast<const blockaggnode*>(node)->basic_block();
	dm[node] = annotate_basic_block(bb, pds);
}

static inline void
annotate_linear(const aggnode * node, variableset & pds, demandmap & dm)
{
	JLM_DEBUG_ASSERT(is<linearaggnode>(node));

	auto ds = demandset::create(pds);
	for (ssize_t n = node->nchildren()-1; n >= 0; n--)
		annotate(node->child(n), pds, dm);
	ds->top = pds;

	dm[node] = std::move(ds);
}

static inline void
annotate_branch(const aggnode * node, variableset & pds, demandmap & dm)
{
	JLM_DEBUG_ASSERT(is<branchaggnode>(node));

	auto ds = branchset::create(pds);

	variableset cases_top;
	for (size_t n = 1; n < node->nchildren(); n++) {
		auto tmp = pds;
		annotate(node->child(n), tmp, dm);
		cases_top.insert(tmp.begin(), tmp.end());
	}
	ds->cases_top = pds = cases_top;

	annotate(node->child(0), pds, dm);
	ds->top = pds;

	dm[node] = std::move(ds);
}

static inline void
annotate_loop(const aggnode * node, variableset & pds, demandmap & dm)
{
	JLM_DEBUG_ASSERT(is<loopaggnode>(node));
	JLM_DEBUG_ASSERT(node->nchildren() == 1);

	auto ds = demandset::create(pds);
	annotate(node->child(0), pds, dm);
	if (ds->bottom != pds) {
		ds->bottom.insert(pds.begin(), pds.end());
		pds = ds->bottom;
		annotate(node->child(0), pds, dm);
	}
	ds->top = ds->bottom;

	dm[node] = std::move(ds);
}

static inline void
annotate(const aggnode * node, variableset & pds, demandmap & dm)
{
	static std::unordered_map<
		std::type_index,
		std::function<void(const aggnode*, variableset&, demandmap&)>
	> map({
	  {typeid(entryaggnode), annotate_entry}, {typeid(exitaggnode), annotate_exit}
	, {typeid(blockaggnode), annotate_block}, {typeid(linearaggnode), annotate_linear}
	, {typeid(branchaggnode), annotate_branch}, {typeid(loopaggnode), annotate_loop}
	});

	auto it = dm.find(node);
	if (it != dm.end() && it->second->bottom == pds) {
		pds = it->second->top;
		return;
	}

	JLM_DEBUG_ASSERT(map.find(typeid(*node)) != map.end());
	return map[typeid(*node)](node, pds, dm);
}

demandmap
annotate(jlm::aggnode & root)
{
	demandmap dm;
	variableset ds;
	annotate(&root, ds, dm);
	return dm;
}

}