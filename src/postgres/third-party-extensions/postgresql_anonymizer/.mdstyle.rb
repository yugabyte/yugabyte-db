all
rule 'MD003', :style => :setext_with_atx
exclude_rule 'MD004'
exclude_rule 'MD012'
# Somehow the latest version of mdl seems to have introduced breaking changes
# we don't have time to investigate, mdl will be replaced by rumdl soon
exclude_rule 'MD013'
#rule "MD013", :tables => false
rule "MD024", :allow_different_nesting => true
rule 'MD025', :level => 2
exclude_rule 'MD026'
exclude_rule 'MD029'
exclude_rule 'MD033'  # To be removed when we switch to rumdl
exclude_rule 'MD034'
exclude_rule 'MD041'
# Disable codeblock_style because we use a mix of fenced and indented in the doc
#rule 'MD046', :style => :consistent
exclude_rule 'MD046'
