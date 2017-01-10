require([
  'dojo/on',
  'dojo/mouse',
  'dojo/dom-attr',
  'dojo/query!css2',
  'codecompass/model',
  'codecompass/viewHandler'],
function (on, mouse, attr, query, model, viewHandler) {
  model.addService('cppservice', 'CppService', LanguageServiceClient);

  function highlightEdges(diagramType, node, svgDom) {
    query('.node', svgDom).forEach(function (node) {
      var edgeAttrCache = {};
      var edgeId = '.edge[id^="' + node.id + '_"] > path';
      on(node, mouse.enter, function () {
        dojo.query(edgeId).forEach(function (edge) {
          edgeAttrCache[node.id] = {
            'stroke'       : attr.has(this, 'stroke')
                           ? attr.get(this, 'stroke')
                           : 'black',
            'stroke-width' : attr.has(this, 'stroke-width')
                           ? attr.get(this, 'stroke-width')
                           : 1
          };
          attr.set(edge, {'stroke' : 'red', 'stroke-width' : 2});
        });
      });
      on(node, mouse.leave, function () {
        dojo.query(edgeId).forEach(function (edge) {
          attr.set(edge, edgeAttrCache[node.id]);
        });
      });
    });
  }

  var astDiagram = {
    id : 'cpp-ast-diagram',

    getDiagram : function (diagramType, nodeId, callback) {
      model.cppservice.getDiagram(nodeId, diagramType, callback);
    },

    mouseOverInfo : function (diagramType, nodeId) {
      var nodeInfo = model.cppservice.getAstNodeInfo(nodeId);
      var range = nodeInfo.range.range;

      return {
        fileId : nodeInfo.range.file,
        selection : [
          range.startpos.line,
          range.startpos.column,
          range.endpos.line,
          range.endpos.column
        ]
      };
    },

    callback : function (diagramType, node, svgDom) {
      highlightEdges(diagramType, node, svgDom);
    }
  };

  viewHandler.registerModule(astDiagram, {
    type : viewHandler.moduleType.Diagram
  });
});
