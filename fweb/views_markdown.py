from django.template.response import TemplateResponse
from django.http import HttpResponse
from django.core.servers.basehttp import FileWrapper
import posixpath
import markdown
import mimetypes

def markdown_page(request, path='', base=''):
    context = {}
    context['path'] = path

    template = 'markdown_page.html'

    absbase = posixpath.split(__file__)[0]
    
    print base, path, posixpath.join(base, path)
    
    if posixpath.splitext(path)[1] in ['.jpg', '.png', '.gif']:
        fullpath = posixpath.join(absbase, base, path)
        
        if posixpath.isfile(fullpath):
            filetype = mimetypes.guess_type(fullpath)[0]
            return HttpResponse(FileWrapper(file(fullpath)), content_type=filetype)
        else:
            return HttpResponse('No such file: %s' % path)
        
    for ext in ['', '.reveal.md', '.md', '.markdown', '.reveal.html', '.html']:
        newpath = posixpath.join(absbase, base, path + ext)

        #print absbase, newpath
        if posixpath.isfile(newpath):
            if not ext:
                ext = posixpath.splitext(path)[1]
                
            with open(newpath, 'r') as f:
                text = f.read()

                if ext == '.reveal.html':
                    html = text
                    template = 'reveal.html'
                elif ext == '.reveal.md':
                    html = text
                    template = 'reveal.html'
                    context['markdown'] = True
                elif ext == '.html':
                    # TODO: extract some metadata from HTML
                    html = text

                    if '<html' in html:
                        # Serve plain HTML
                        return HttpResponse(html, content_type="text/html")
                else:
                    md = markdown.Markdown(extensions = ['markdown.extensions.meta', 'markdown.extensions.attr_list'], safe_mode='escape')
                    html = md.convert(text.decode('utf-8'))
                    context['meta'] = md.Meta
                    
                context['html'] = html

            break

    return TemplateResponse(request, template, context=context)
