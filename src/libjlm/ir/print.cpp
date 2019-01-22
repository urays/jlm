/*
 * Copyright 2017 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include <jlm/jlm/ir/aggregation.hpp>
#include <jlm/jlm/ir/basic-block.hpp>
#include <jlm/jlm/ir/cfg.hpp>
#include <jlm/jlm/ir/module.hpp>
#include <jlm/jlm/ir/operators/operators.hpp>
#include <jlm/jlm/ir/print.hpp>
#include <jlm/jlm/ir/tac.hpp>

#include <deque>

namespace jlm {

/* string converters */

/* FIXME: replace with traverser */
static inline std::vector<jlm::cfg_node*>
breadth_first_traversal(const jlm::cfg & cfg)
{
	std::deque<jlm::cfg_node*> next({cfg.entry()});
	std::vector<jlm::cfg_node*> nodes({cfg.entry()});
	std::unordered_set<jlm::cfg_node*> visited({cfg.entry()});
	while (!next.empty()) {
		auto node = next.front();
		next.pop_front();

		for (auto it = node->begin_outedges(); it != node->end_outedges(); it++) {
			if (visited.find(it->sink()) == visited.end()) {
				visited.insert(it->sink());
				next.push_back(it->sink());
				nodes.push_back(it->sink());
			}
		}
	}

	return nodes;
}

static std::string
emit_tac(const jlm::tac &);

static std::string
emit_tacs(const tacsvector_t & tacs)
{
	std::string str;
	for (const auto & tac : tacs)
		str += emit_tac(*tac) + ", ";

	return "[" + str + "]";
}

static inline std::string
emit_global(const jlm::gblvalue * v)
{
	auto init = v->node()->initialization();

	std::string str = v->debug_string();
	if (init)
		str += " = " + emit_tacs(init->tacs());

	return str;
}

static inline std::string
emit_entry(const jlm::cfg_node * node)
{
	JLM_DEBUG_ASSERT(is<entry_node>(node));
	auto & en = *static_cast<const jlm::entry_node*>(node);

	std::string str;
	for (size_t n = 0; n < en.narguments(); n++)
		str += en.argument(n)->debug_string() + " ";

	return str + "\n";
}

static inline std::string
emit_exit(const jlm::cfg_node * node)
{
	JLM_DEBUG_ASSERT(is<exit_node>(node));
	auto & xn = *static_cast<const jlm::exit_node*>(node);

	std::string str;
	for (size_t n = 0; n < xn.nresults(); n++)
		str += xn.result(n)->debug_string() + " ";

	return str;
}

static inline std::string
emit_tac(const jlm::tac & tac)
{
	/* convert results */
	std::string results;
	for (size_t n = 0; n < tac.noutputs(); n++) {
		results += tac.output(n)->debug_string();
		if (n != tac.noutputs()-1)
			results += ", ";
	}

	/* convert operands */
	std::string operands;
	for (size_t n = 0; n < tac.ninputs(); n++) {
		operands += tac.input(n)->debug_string();
		if (n != tac.ninputs()-1)
			operands += ", ";
	}

	std::string op = tac.operation().debug_string();
	return results + (results.empty() ? "" : " = ") + op + " " + operands;
}

static inline std::string
emit_label(const jlm::cfg_node * node)
{
	return strfmt(node);
}

static inline std::string
emit_targets(const jlm::cfg_node * node)
{
	size_t n = 0;
	std::string str("[");
	for (auto it = node->begin_outedges(); it != node->end_outedges(); it++, n++) {
		str += emit_label(it->sink());
		if (n != node->noutedges()-1)
			str += ", ";
	}
	str += "]";

	return str;
}

static inline std::string
emit_basic_block(const jlm::cfg_node * node)
{
	JLM_DEBUG_ASSERT(is<basic_block>(node));
	auto & tacs = static_cast<const basic_block*>(node)->tacs();

	std::string str;
	for (const auto & tac : tacs) {
		str += "\t" + emit_tac(*tac);
		if (tac != tacs.last())
			str += "\n";
	}

	if (tacs.last()) {
		if (is<branch_op>(tacs.last()->operation()))
			str += " " + emit_targets(node);
		else
			str += "\n\t" + emit_targets(node);
	}	else {
		str += "\t" + emit_targets(node);
	}

	return str + "\n";
}

std::string
to_str(const jlm::cfg & cfg)
{
	static
	std::unordered_map<std::type_index, std::string(*)(const cfg_node*)> map({
	  {typeid(entry_node), emit_entry}
	, {typeid(exit_node), emit_exit}
	, {typeid(basic_block), emit_basic_block}
	});

	std::string str;
	auto nodes = breadth_first_traversal(cfg);
	for (const auto & node : nodes) {
		str += emit_label(node) + ":";
		str += (is<basic_block>(node) ? "\n" : " ");

		JLM_DEBUG_ASSERT(map.find(typeid(*node)) != map.end());
		str += map[typeid(*node)](node) + "\n";
	}

	return str;
}

static std::string
emit_function_node(const jlm::ipgraph_node & clg_node)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const function_node*>(&clg_node));
	auto & node = *static_cast<const function_node*>(&clg_node);

	const auto & fcttype = node.fcttype();

	/* convert result types */
	std::string results("<");
	for(size_t n = 0; n < fcttype.nresults(); n++) {
		results += fcttype.result_type(n).debug_string();
		if (n != fcttype.nresults()-1)
			results += ", ";
	}
	results += ">";

	/* convert operand types */
	std::string operands("<");
	for (size_t n = 0; n < fcttype.narguments(); n++) {
		operands += fcttype.argument_type(n).debug_string();
		if (n != fcttype.narguments()-1)
			operands += ", ";
	}
	operands += ">";

	std::string cfg = node.cfg() ? to_str(*node.cfg()) : "";
	std::string exported = !is_externally_visible(node.linkage()) ? "static" : "";

	return exported + results + " " + node.name() + " " + operands + "\n{\n" + cfg + "}\n";
}

static std::string
emit_data_node(const jlm::ipgraph_node & clg_node)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const data_node*>(&clg_node));
	auto & node = *static_cast<const data_node*>(&clg_node);
	auto init = node.initialization();

	std::string str = node.name();
	if (init)
		str += " = " + emit_tacs(init->tacs());

	return str;
}

std::string
to_str(const jlm::ipgraph & clg)
{
	static std::unordered_map<
		std::type_index,
		std::function<std::string(const jlm::ipgraph_node&)>
	> map({
		{typeid(function_node), emit_function_node}
	, {typeid(data_node), emit_data_node}
	});

	std::string str;
	for (const auto & node : clg) {
		JLM_DEBUG_ASSERT(map.find(typeid(node)) != map.end());
		str += map[typeid(node)](node) + "\n";
	}

	return str;
}

/* dot converters */

static inline std::string
emit_entry_dot(const jlm::cfg_node & node)
{
	JLM_DEBUG_ASSERT(is<entry_node>(&node));
	auto en = static_cast<const jlm::entry_node*>(&node);

	std::string str;
	for (size_t n = 0; n < en->narguments(); n++) {
		auto argument = en->argument(n);
		str += "<" + argument->type().debug_string() + "> " + argument->name() + "\\n";
	}

	return str;
}

static inline std::string
emit_exit_dot(const jlm::cfg_node & node)
{
	JLM_DEBUG_ASSERT(is<exit_node>(&node));
	auto xn = static_cast<const jlm::exit_node*>(&node);

	std::string str;
	for (size_t n = 0; n < xn->nresults(); n++) {
		auto result = xn->result(n);
		str += "<" + result->type().debug_string() + "> " + result->name() + "\\n";
	}

	return str;
}

static inline std::string
to_dot(const jlm::tac & tac)
{
	std::string str;
	if (tac.noutputs() != 0) {
		for (size_t n = 0; n < tac.noutputs()-1; n++)
			str +=  tac.output(n)->debug_string() + ", ";
		str += tac.output(tac.noutputs()-1)->debug_string() + " = ";
	}

	str += tac.operation().debug_string();

	if (tac.ninputs() != 0) {
		str += " ";
		for (size_t n = 0; n < tac.ninputs()-1; n++)
			str += tac.input(n)->debug_string() + ", ";
		str += tac.input(tac.ninputs()-1)->debug_string();
	}

	return str;
}

static inline std::string
emit_basic_block(const cfg_node & node)
{
	JLM_DEBUG_ASSERT(is<basic_block>(&node));
	auto & tacs = static_cast<const basic_block*>(&node)->tacs();

	std::string str;
	for (const auto & tac : tacs)
		str += emit_tac(*tac) + "\\n";

	return str;
}

static inline std::string
emit_header(const jlm::cfg_node & node)
{
	if (is<entry_node>(&node))
		return "ENTRY";

	if (is<exit_node>(&node))
		return "EXIT";

	return strfmt(&node);
}

static inline std::string
emit_node(const jlm::cfg_node & node)
{
	static
	std::unordered_map<std::type_index, std::string(*)(const cfg_node &)> map({
	  {typeid(entry_node), emit_entry_dot}
	, {typeid(exit_node), emit_exit_dot}
	, {typeid(basic_block), emit_basic_block}
	});

	JLM_DEBUG_ASSERT(map.find(typeid(node)) != map.end());
	std::string body = map[typeid(node)](node);

	return emit_header(node) + "\\n" + body;
}

std::string
to_dot(const jlm::cfg & cfg)
{
	auto entry = cfg.entry();
	auto exit = cfg.exit();

	std::string dot("digraph cfg {\n");

	/* emit entry node */
	dot += strfmt("{ rank = source; ", (intptr_t)entry, "[shape=box, label = \"",
		emit_node(*entry), "\"]; }\n");
	dot += strfmt((intptr_t)entry, " -> ", (intptr_t)entry->outedge(0)->sink(), "[label=\"0\"];\n");


	/* emit exit node */
	dot += strfmt("{ rank = sink; ", (intptr_t)exit, "[shape=box, label = \"",
		emit_node(*exit), "\"]; }\n");

	for (const auto & node : cfg) {
		dot += strfmt("{", (intptr_t)&node);
		dot += strfmt("[shape = box, label = \"", emit_node(node), "\"]; }\n");
		for (auto it = node.begin_outedges(); it != node.end_outedges(); it++) {
			dot += strfmt((intptr_t)it->source(), " -> ", (intptr_t)it->sink());
			dot += strfmt("[label = \"", it->index(), "\"];\n");
		}
	}
	dot += "}\n";

	return dot;
}

std::string
to_dot(const jlm::ipgraph & clg)
{
	std::string dot("digraph clg {\n");
	for (const auto & node : clg) {
		dot += strfmt((intptr_t)&node);
		dot += strfmt("[label = \"", node.name(), "\"];\n");

		for (const auto & call : node)
			dot += strfmt((intptr_t)&node, " -> ", (intptr_t)call, ";\n");
	}
	dot += "}\n";

	return dot;
}

/* aggregation node */

static std::string
emit_variableset(const variableset & ds)
{
	std::string s("{");
	for (auto it = ds.begin(); it != ds.end(); it++) {
		s += (*it)->name();
		if (std::next(it) != ds.end())
			s += ", ";
	}
	s += "}";

	return s;
}

static std::string
emit_demandset(const demandset & ds)
{
	return emit_variableset(ds.bottom) + " -> " + emit_variableset(ds.top)
	     + " [" + emit_variableset(ds.reads) + "] [" + emit_variableset(ds.writes) + "]";
}

std::string
to_str(const aggnode & n, const demandmap & dm)
{
  std::function<std::string(const aggnode&, size_t)> f = [&] (
    const aggnode & n,
    size_t depth
  ) {
    std::string subtree(depth, '-');
    subtree += n.debug_string();

		auto it = dm.find(&n);
		subtree += (it != dm.end() ? " " + emit_demandset(*it->second) : "") + "\n";

    for (const auto & child : n)
      subtree += f(child, depth+1);

    return subtree;
  };

  return f(n, 0);
}

void
print(const aggnode & n, const demandmap & dm, FILE * out)
{
	fputs(to_str(n, dm).c_str(), out);
	fflush(out);
}

}